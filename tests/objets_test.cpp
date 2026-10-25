// =============================================================================
//  tests/objets_test.cpp - 1.13.0 (lot J1) : LES OBJETS
// -----------------------------------------------------------------------------
//  Les types IHM deviennent de vrais objets : des METHODES qui voient leurs
//  membres par SELF, des membres (et des methodes) PRIVES, l'HERITAGE (EXTENDS,
//  VIRTUAL/OVERRIDE/SUPER) et les INTERFACES (IMPLEMENTS, REF_TO I_xxx, le
//  dispatch sur le type reel, DYNAMIC_CAST).
//
//  Tout cela n'est QUE du dialecte IHM : le ST de l'automate ne connait ni
//  METHOD, ni EXTENDS, ni INTERFACE, ni SELF ; ses essais restent verts.
//
//  OBJ_OBS=1 : chaque observation est ecrite (pas seulement les echecs).
// =============================================================================
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiMethods.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>
#include <vector>

using namespace hmi;

namespace {

int g_checks = 0, g_failures = 0;
const bool g_obs = [] { const char* d = std::getenv("OBJ_OBS"); return d && *d == '1'; }();

void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::printf("  ECHEC  %s\n", what.c_str());
    } else if (g_obs) {
        std::printf("  ok     %s\n", what.c_str());
    }
}

// ---- de petits assembleurs de modele -----------------------------------------
TypeMember member(const std::string& name, const std::string& type, Visibility vis = Visibility::Public,
                  const std::string& initial = {}) {
    TypeMember m;
    m.name = name;
    m.type = type;
    m.visibility = vis;
    m.initial = initial;
    return m;
}
[[maybe_unused]] Declaration param(Project& p, const std::string& name, const std::string& type, PassMode mode = PassMode::In) {
    Declaration d;
    d.id = p.allocate();
    d.kind = DeclKind::Parameter;
    d.name = name;
    d.type = type;
    d.mode = mode;
    return d;
}
HmiFunction method(Project& p, const std::string& name, const std::string& ret, const std::string& body,
                   Visibility vis = Visibility::Public, bool isVirtual = false,
                   std::vector<Declaration> decls = {}) {
    HmiFunction f;
    f.id = p.allocate();
    f.name = name;
    f.returnType = ret;
    f.body = body;
    f.visibility = vis;
    f.isVirtual = isVirtual;
    f.decls = std::move(decls);
    return f;
}
HmiType& addType(Project& p, const std::string& name, HmiTypeKind kind = HmiTypeKind::Structure) {
    HmiType t;
    t.id = p.allocate();
    t.name = name;
    t.kind = kind;
    p.programs.types.push_back(std::move(t));
    return p.programs.types.back();
}
[[maybe_unused]] Variable& addVar(Project& p, const std::string& n, const std::string& t, const std::string& init = {}) {
    Variable v;
    v.id = p.allocate();
    v.name = n;
    v.type = t;
    v.initial = init;
    p.programs.variables.push_back(v);
    return p.programs.variables.back();
}

// un lecteur de fichiers en memoire, pour relire ce que serializeProject a ecrit
std::function<bool(const std::string&, std::string&)> readerOf(const std::vector<ProjectFile>& files) {
    return [&files](const std::string& path, std::string& content) {
        for (const auto& f : files)
            if (f.path == path && f.data) {
                content.assign(f.data->begin(), f.data->end());
                return true;
            }
        return false;
    };
}
std::string indexText(const std::vector<ProjectFile>& files) {
    for (const auto& f : files)
        if (f.path == "ihm.txt" && f.data) return std::string(f.data->begin(), f.data->end());
    return {};
}

// =============================================================================
//  1) SERIALISATION : un type a objets relu identique ; le cran de format 31 ;
//     le repli ; le refus d'un format trop recent.
// =============================================================================
void serialisation() {
    std::printf("-- serialisation : methodes, heritage, interfaces, membre prive (format 31)\n");
    Project p;

    // Une interface I_Demarrable : deux signatures, pas de membre, pas de corps.
    HmiType& iface = addType(p, "I_Demarrable", HmiTypeKind::Interface);
    iface.methods.push_back(method(p, "Demarrer", {}, {}));
    iface.methods.push_back(method(p, "EnDefaut", "BOOL", {}));

    // T_Pompe : un membre public, un prive ; trois methodes (publique, publique virtuelle, privee).
    HmiType& pompe = addType(p, "T_Pompe");
    pompe.interfaces = {"I_Demarrable"};
    pompe.members.push_back(member("Nom", "STRING"));
    pompe.members.push_back(member("Debit", "REAL"));
    pompe.members.push_back(member("Heures", "REAL", Visibility::Private, "0.0"));
    pompe.methods.push_back(method(p, "Demarrer", {}, "Marche := TRUE;"));
    pompe.methods.push_back(method(p, "EnDefaut", "BOOL", "EnDefaut := Debit < 1.0;", Visibility::Public, true));
    pompe.methods.push_back(method(p, "Journal", {}, "Heures := Heures + 1.0;", Visibility::Private));
    pompe.members.push_back(member("Marche", "BOOL"));

    // T_PompeDoublee EXTENDS T_Pompe, OVERRIDE EnDefaut avec un parametre et des locales.
    HmiType& doublee = addType(p, "T_PompeDoublee");
    doublee.base = "T_Pompe";
    doublee.members.push_back(member("Secours", "BOOL"));
    {
        HmiFunction ov = method(p, "EnDefaut", "BOOL", "EnDefaut := SUPER.EnDefaut() AND NOT Secours;", Visibility::Public, false);
        doublee.methods.push_back(ov);
    }

    const auto files = serializeProject(p);
    const std::string idx = indexText(files);
    check(idx.find("(format 31)") != std::string::npos, "l'index dit (format 31) quand un type a des objets");
    check(idx.find("methode_type") != std::string::npos, "le mot-cle methode_type est ecrit");
    check(idx.find("herite=\"T_Pompe\"") != std::string::npos, "l'heritage est ecrit (herite=T_Pompe)");
    check(idx.find("implemente=\"I_Demarrable\"") != std::string::npos, "l'interface implementee est ecrite");
    check(idx.find("genre=interface") != std::string::npos, "le genre interface est ecrit");
    check(idx.find("portee=privee") != std::string::npos, "un membre/une methode privee est marquee privee");

    auto relu = parseProject(readerOf(files));
    check(static_cast<bool>(relu), "le projet se relit");
    if (relu) {
        const Project& q = *relu;
        const HmiType* ri = nullptr;
        const HmiType* rp = nullptr;
        const HmiType* rd = nullptr;
        for (const auto& t : q.programs.types) {
            if (t.name == "I_Demarrable") ri = &t;
            if (t.name == "T_Pompe") rp = &t;
            if (t.name == "T_PompeDoublee") rd = &t;
        }
        check(ri && ri->kind == HmiTypeKind::Interface && ri->methods.size() == 2,
              "l'interface est relue (genre Interface, 2 signatures)");
        check(ri && ri->methods.size() == 2 && ri->methods[1].name == "EnDefaut" && ri->methods[1].returnType == "BOOL",
              "une signature d'interface garde son retour");
        if (rp) {
            check(rp->interfaces.size() == 1 && rp->interfaces[0] == "I_Demarrable", "T_Pompe garde IMPLEMENTS I_Demarrable");
            check(rp->methods.size() == 3, "T_Pompe garde ses 3 methodes");
            bool privMember = false;
            for (const auto& m : rp->members)
                if (m.name == "Heures" && m.visibility == Visibility::Private) privMember = true;
            check(privMember, "le membre prive Heures est relu prive");
            const HmiFunction* en = nullptr;
            const HmiFunction* jr = nullptr;
            for (const auto& m : rp->methods) {
                if (m.name == "EnDefaut") en = &m;
                if (m.name == "Journal") jr = &m;
            }
            check(en && en->isVirtual && en->returnType == "BOOL" && en->body.find("Debit < 1.0") != std::string::npos,
                  "EnDefaut est relue virtuelle, retour BOOL, corps identique");
            check(jr && jr->visibility == Visibility::Private, "Journal est relue privee");
        } else {
            check(false, "T_Pompe est relu");
        }
        if (rd) {
            check(rd->base == "T_Pompe", "T_PompeDoublee garde EXTENDS T_Pompe");
            check(rd->methods.size() == 1 && rd->methods[0].name == "EnDefaut"
                      && rd->methods[0].body.find("SUPER.EnDefaut()") != std::string::npos,
                  "l'OVERRIDE de T_PompeDoublee garde son corps avec SUPER");
        } else {
            check(false, "T_PompeDoublee est relu");
        }
    }

    // Repli : un type ordinaire (sans objet) ne monte pas au format 31.
    Project plain;
    HmiType& simple = addType(plain, "T_Vecteur");
    simple.members.push_back(member("x", "REAL"));
    simple.members.push_back(member("y", "REAL"));
    const std::string plainIdx = indexText(serializeProject(plain));
    check(plainIdx.find("(format 31)") == std::string::npos, "un type ordinaire NE monte PAS au format 31 (repli)");

    // Refus d'un format trop recent (32).
    auto tooNew = parseProject([](const std::string& path, std::string& content) {
        if (path != "ihm.txt") return false;
        content = "# IHM Studio - projet IHM (format 32)\nihm format=32 prochain_id=1\n";
        return true;
    });
    check(!tooNew, "un format 32 (plus recent) est refuse proprement");
}

} // namespace

// --- declarations des etapes suivantes (definies plus bas dans ce fichier) ---
void modeleHelpers();
void moteur();
void controle();

int main() {
    std::printf("1.13.0 (lot J1) : les objets - methodes, prive, heritage, interfaces\n");
    serialisation();
    modeleHelpers();
    controle();
    moteur();
    std::printf("%d controles, %d echec(s)\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

// =============================================================================
//  Un petit projet d'essai partage : I_Demarrable, T_Pompe (IMPLEMENTS), un
//  membre prive Heures, une virtuelle EnDefaut, une privee Journal ; T_PompeDoublee
//  EXTENDS T_Pompe (OVERRIDE EnDefaut) ; T_Vanne IMPLEMENTS I_Demarrable.
// =============================================================================
namespace {
Project buildProject() {
    Project p;
    HmiType& iface = addType(p, "I_Demarrable", HmiTypeKind::Interface);
    iface.methods.push_back(method(p, "Demarrer", {}, {}));
    iface.methods.push_back(method(p, "EnDefaut", "BOOL", {}));

    HmiType& pompe = addType(p, "T_Pompe");
    pompe.interfaces = {"I_Demarrable"};
    pompe.members.push_back(member("Nom", "STRING"));
    pompe.members.push_back(member("Debit", "REAL"));
    pompe.members.push_back(member("Marche", "BOOL"));
    pompe.members.push_back(member("Heures", "REAL", Visibility::Private, "0.0"));
    pompe.methods.push_back(method(p, "Demarrer", {}, "Marche := TRUE;\nIF Heures > 8000.0 THEN Journal(); END_IF;"));
    pompe.methods.push_back(method(p, "EnDefaut", "BOOL", "EnDefaut := Marche AND Debit < 1.0;", Visibility::Public, true));
    pompe.methods.push_back(method(p, "Journal", {}, "Heures := Heures + 1.0;", Visibility::Private));

    HmiType& doublee = addType(p, "T_PompeDoublee");
    doublee.base = "T_Pompe";
    doublee.members.push_back(member("Secours", "BOOL"));
    doublee.methods.push_back(method(p, "EnDefaut", "BOOL", "EnDefaut := SUPER.EnDefaut() AND NOT Secours;", Visibility::Public, false));

    HmiType& vanne = addType(p, "T_Vanne");
    vanne.interfaces = {"I_Demarrable"};
    vanne.members.push_back(member("Position", "REAL"));
    vanne.methods.push_back(method(p, "Demarrer", {}, "Position := 100.0;"));
    vanne.methods.push_back(method(p, "EnDefaut", "BOOL", "EnDefaut := Position < 1.0;"));
    return p;
}
} // namespace

// =============================================================================
//  2) LES HELPERS DE MODELE : heritage, methodes effectives, interfaces, le texte
//     de la FUNCTION interne (SELF en VAR_IN_OUT).
// =============================================================================
void modeleHelpers() {
    std::printf("-- helpers de modele : heritage, methodes, interfaces, texte interne\n");
    Project p = buildProject();

    // allMembers : la tranche du parent D'ABORD, puis les siens.
    const auto mm = types::membersOf(p, "T_PompeDoublee");
    check(mm.size() == 5, "T_PompeDoublee a 5 membres (4 du parent + Secours)");
    check(mm.size() == 5 && mm[0].name == "Nom" && mm[3].name == "Heures" && mm[4].name == "Secours",
          "les membres du parent (place Modbus) d'abord, Secours en dernier");

    // baseChain, isA, implementsInterface.
    check(baseChain(p, "T_PompeDoublee").size() == 1 && baseChain(p, "T_PompeDoublee")[0]->name == "T_Pompe",
          "baseChain(T_PompeDoublee) = [T_Pompe]");
    check(isA(p, "T_PompeDoublee", "T_Pompe") && !isA(p, "T_Pompe", "T_PompeDoublee"),
          "un sous-type isA son parent, pas l'inverse");
    check(implementsInterface(p, "T_Pompe", "I_Demarrable") && implementsInterface(p, "T_PompeDoublee", "I_Demarrable"),
          "T_Pompe et son sous-type implementent I_Demarrable (herite)");
    check(!implementsInterface(p, "T_Vanne", "I_Introuvable"), "T_Vanne n'implemente pas une interface inconnue");

    // effectiveMethod : l'OVERRIDE l'emporte ; la methode heritee sinon.
    const HmiType* owner = nullptr;
    const HmiFunction* en = effectiveMethod(p, "T_PompeDoublee", "EnDefaut", &owner);
    check(en && owner && owner->name == "T_PompeDoublee" && en->body.find("SUPER") != std::string::npos,
          "effectiveMethod(T_PompeDoublee, EnDefaut) = l'OVERRIDE du sous-type");
    const HmiFunction* dem = effectiveMethod(p, "T_PompeDoublee", "Demarrer", &owner);
    check(dem && owner && owner->name == "T_Pompe", "Demarrer est herite de T_Pompe (pas redefini)");
    check(allMethods(p, "T_PompeDoublee").size() == 3, "T_PompeDoublee voit 3 methodes (Demarrer, EnDefaut, Journal)");

    // methodFunctionText : SELF en VAR_IN_OUT, membres prefixes, THIS->SELF, retour renomme.
    const HmiFunction* enP = effectiveMethod(p, "T_Pompe", "EnDefaut");
    const std::string txt = methodFunctionText(p, "T_Pompe", *enP);
    check(txt.find("VAR_IN_OUT") != std::string::npos && txt.find("SELF : T_Pompe") != std::string::npos,
          "methodFunctionText declare VAR_IN_OUT SELF : T_Pompe");
    check(txt.find("SELF.Marche") != std::string::npos && txt.find("SELF.Debit") != std::string::npos,
          "les membres sont prefixes de SELF. (SELF.Marche, SELF.Debit)");
    check(txt.find("METHODE_T_POMPE_ENDEFAUT :=") != std::string::npos,
          "le retour (EnDefaut :=) est renomme vers le nom interne");

    // la methode Demarrer appelle la privee Journal() : l'appel reste (le moteur le resoudra).
    const HmiFunction* demP = effectiveMethod(p, "T_Pompe", "Demarrer");
    const std::string demTxt = methodFunctionText(p, "T_Pompe", *demP);
    check(demTxt.find("SELF.Heures") != std::string::npos, "Demarrer : SELF.Heures prefixe");
    check(demTxt.find("Journal(") != std::string::npos, "l'appel d'une autre methode (Journal()) reste tel quel");

    // SUPER.EnDefaut() -> METHODE_T_POMPE_ENDEFAUT(SELF) dans l'OVERRIDE.
    const std::string ovTxt = methodFunctionText(p, "T_PompeDoublee", *en);
    check(ovTxt.find("METHODE_T_POMPE_ENDEFAUT(SELF)") != std::string::npos,
          "SUPER.EnDefaut() devient METHODE_T_POMPE_ENDEFAUT(SELF)");

    // resolveMethod : l'interface avec l'interpreteur.
    MethodCall mc;
    check(resolveMethod(p, "T_PompeDoublee", "EnDefaut", mc) && mc.selfType == "T_PompeDoublee" && !mc.function.empty(),
          "resolveMethod(T_PompeDoublee, EnDefaut) trouve l'OVERRIDE");
    MethodCall mc2;
    check(resolveMethod(p, "T_PompeDoublee", "Demarrer", mc2) && mc2.selfType == "T_Pompe",
          "resolveMethod(T_PompeDoublee, Demarrer) remonte a T_Pompe");
    check(!resolveMethod(p, "T_Pompe", "Inexistante", mc2), "resolveMethod echoue sur une methode inconnue");

    // interfaceMisses : conforme vs manquant (nomme).
    check(interfaceMisses(p, *p.hmiTypeByName("T_Pompe")).empty(), "T_Pompe est conforme a I_Demarrable");
    {
        Project q = buildProject();
        HmiType* v = nullptr;
        for (auto& t : q.programs.types) if (t.name == "T_Vanne") v = &t;
        // enlever EnDefaut : l'interface n'est plus satisfaite.
        for (std::size_t i = 0; v && i < v->methods.size(); ++i)
            if (v->methods[i].name == "EnDefaut") { v->methods.erase(v->methods.begin() + static_cast<long>(i)); break; }
        const auto miss = interfaceMisses(q, *v);
        bool named = false;
        for (const auto& s : miss) if (s.find("EnDefaut") != std::string::npos && s.find("I_Demarrable") != std::string::npos) named = true;
        check(named, "une interface non satisfaite nomme la methode manquante (EnDefaut, I_Demarrable)");
    }

    // une interface ne s'emploie que par REF_TO (c'est un contrat, pas une donnee directe).
    {
        std::string why;
        check(!types::validMemberType(p, "I_Demarrable", &why) && why.find("REF_TO") != std::string::npos,
              "une interface comme donnee directe (membre) est refusee (emploie REF_TO)");
        check(types::validMemberType(p, "LIST OF REF_TO I_Demarrable", &why), "LIST OF REF_TO I_Demarrable est accepte");
        check(!types::validMemberType(p, "LIST OF I_Demarrable", &why), "LIST OF I_Demarrable (sans REF_TO) est refuse");
        // au niveau d'une declaration, REF_TO I_Demarrable se resout (l'interface est referencable).
        const auto ref = typereg::Registry::build(p)->resolve("REF_TO I_Demarrable", typereg::UseDeclaration);
        check(ref.ok, "REF_TO I_Demarrable se resout comme type d'une declaration");
    }
}

void controle() {}
void moteur() {}
