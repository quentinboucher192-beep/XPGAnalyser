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
#include "../src/hmi/HmiCheck.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/hmi/HmiMethods.hpp"
#include "../src/hmi/HmiRuntime.hpp"
#include "../src/hmi/HmiStore.hpp"
#include "../src/hmi/HmiTypes.hpp"
#include "../src/hmi/HmiTypeRegistry.hpp"
#include "../src/sim/Interpreter.hpp"

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
    check(resolveMethod(p, "T_PompeDoublee", "Demarrer", mc2) && mc2.selfType == "T_Pompe"
              && mc2.function.find("SUPER") == std::string::npos,
          "resolveMethod(T_PompeDoublee, Demarrer) resout l'heritee (SELF : T_Pompe, le definisseur)");
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

// =============================================================================
//  3) LE CONTROLE (Generer) : ce qui refuse une mauvaise definition de type.
// =============================================================================
namespace {
std::vector<Issue> generOf(const Project& p) {
    return generate(p, [](std::string_view) { return false; });
}
bool hasError(const std::vector<Issue>& is, const std::string& needle) {
    for (const auto& i : is)
        if (i.severity == Issue::Severity::Error && i.message.find(needle) != std::string::npos) return true;
    return false;
}
} // namespace

void controle() {
    std::printf("-- controle Generer : EXTENDS, OVERRIDE/VIRTUAL, interfaces, noms\n");

    // EXTENDS hors structure : heriter d'une enumeration.
    {
        Project p;
        HmiType& e = addType(p, "T_Mode", HmiTypeKind::Enumeration);
        HmiEnumValue v; v.name = "Auto"; e.values.push_back(v);
        HmiType& t = addType(p, "T_X");
        t.base = "T_Mode";
        t.members.push_back(member("a", "INT"));
        check(hasError(generOf(p), "ne peut h\xC3\xA9riter que d'une structure"), "EXTENDS hors structure refuse");
    }
    // EXTENDS parent inconnu (veux-tu dire ?).
    {
        Project p;
        addType(p, "T_Pompe").members.push_back(member("a", "INT"));
        HmiType& t = addType(p, "T_X");
        t.base = "T_Pompes";                                   // faute de frappe
        t.members.push_back(member("b", "INT"));
        check(hasError(generOf(p), "type inconnu") && hasError(generOf(p), "veux-tu dire T_Pompe"),
              "EXTENDS d'un parent inconnu : refuse, veux-tu dire ?");
    }
    // Cycle d'heritage.
    {
        Project p;
        HmiType& a = addType(p, "T_A"); a.base = "T_B"; a.members.push_back(member("x", "INT"));
        HmiType& b = addType(p, "T_B"); b.base = "T_A"; b.members.push_back(member("y", "INT"));
        check(hasError(generOf(p), "h\xC3\xA9rite de lui-m\xC3\xAAme"), "cycle d'heritage refuse");
    }
    // OVERRIDE sans VIRTUAL au parent.
    {
        Project p;
        HmiType& base = addType(p, "T_Base");
        base.members.push_back(member("v", "INT"));
        base.methods.push_back(method(p, "M", "INT", "M := 1;", Visibility::Public, false));   // pas virtuelle
        HmiType& sub = addType(p, "T_Sub");
        sub.base = "T_Base";
        sub.methods.push_back(method(p, "M", "INT", "M := 2;"));
        check(hasError(generOf(p), "qui n'est pas virtuelle"), "OVERRIDE sans VIRTUAL refuse");
    }
    // OVERRIDE de signature differente.
    {
        Project p;
        HmiType& base = addType(p, "T_Base");
        base.members.push_back(member("v", "INT"));
        base.methods.push_back(method(p, "M", "INT", "M := 1;", Visibility::Public, true));    // virtuelle : INT
        HmiType& sub = addType(p, "T_Sub");
        sub.base = "T_Base";
        sub.methods.push_back(method(p, "M", "REAL", "M := 2.0;"));                            // retour REAL
        check(hasError(generOf(p), "change sa signature"), "OVERRIDE de signature differente refuse");
    }
    // Une methode privee ne peut pas etre virtuelle.
    {
        Project p;
        HmiType& t = addType(p, "T_X");
        t.members.push_back(member("v", "INT"));
        t.methods.push_back(method(p, "Secret", {}, "v := 0;", Visibility::Private, true));
        check(hasError(generOf(p), "ne peut pas \xC3\xAatre virtuelle"), "methode privee virtuelle refusee");
    }
    // Interface non satisfaite : la methode manquante est NOMMEE.
    {
        Project p;
        HmiType& iface = addType(p, "I_Demarrable", HmiTypeKind::Interface);
        iface.methods.push_back(method(p, "Demarrer", {}, {}));
        iface.methods.push_back(method(p, "EnDefaut", "BOOL", {}));
        HmiType& t = addType(p, "T_Vanne");
        t.interfaces = {"I_Demarrable"};
        t.members.push_back(member("Position", "REAL"));
        t.methods.push_back(method(p, "Demarrer", {}, "Position := 100.0;"));   // EnDefaut manque
        const auto is = generOf(p);
        check(hasError(is, "EnDefaut") && hasError(is, "I_Demarrable"), "interface non satisfaite : EnDefaut nommee");
    }
    // Un membre et une methode ne partagent pas le nom.
    {
        Project p;
        HmiType& t = addType(p, "T_X");
        t.members.push_back(member("Etat", "INT"));
        t.methods.push_back(method(p, "Etat", "INT", "Etat := 0;"));
        check(hasError(generOf(p), "la fois un membre et une m\xC3\xA9thode"), "membre et methode de meme nom refuses");
    }
    // Une interface ne porte pas de membre.
    {
        Project p;
        HmiType& iface = addType(p, "I_X", HmiTypeKind::Interface);
        iface.members.push_back(member("a", "INT"));
        iface.methods.push_back(method(p, "M", {}, {}));
        check(hasError(generOf(p), "ne porte pas de membre"), "une interface avec un membre refusee");
    }
    // Une interface comme donnee directe (membre sans REF_TO).
    {
        Project p;
        addType(p, "I_D", HmiTypeKind::Interface).methods.push_back(method(p, "M", {}, {}));
        HmiType& t = addType(p, "T_X");
        t.members.push_back(member("ref", "I_D"));       // sans REF_TO
        check(hasError(generOf(p), "REF_TO I_D"), "interface comme donnee directe refusee (emploie REF_TO)");
    }
}

// =============================================================================
//  4) LE MOTEUR : une methode tourne en simulation (SELF, procedure/fonction,
//     appel sur variable/case/membre compose), l'heritage, le dispatch virtuel.
// =============================================================================
namespace {
std::string shown(const sim::Value& v) { return v.type() == sim::Type::String ? v.asString() : v.display(); }

// Un projet, un script "Essai", et ce qu'il laisse dans la variable `readVar` (ou "#").
std::string run(Project& p, const std::string& scriptBody, std::string* err = nullptr, const std::string& readVar = "R") {
    Script s;
    s.id = p.allocate();
    s.name = "Essai";
    s.event = "Appel";
    s.body = scriptBody;
    p.programs.scripts.push_back(s);
    View v = makeView(p, "Vue_Essai");
    p.views.push_back(v);
    p.config.startView = p.views.back().id;
    Runtime rt;
    rt.bind(&p, nullptr);
    rt.start(0.0);
    rt.tick(0.1);
    std::string e;
    const bool ok = rt.callScript("Essai", 0.2, &e);
    if (err) *err = ok ? std::string{} : e;
    sim::Value out;
    return rt.environment().read(readVar, out) ? shown(out) : std::string("#");
}

// Un projet "compteur" : T_Compteur { N : INT } avec Incr() (procedure, ecrit SELF.N)
// et Double() : INT (fonction, lit SELF.N).
Project counterProject() {
    Project p;
    HmiType& c = addType(p, "T_Compteur");
    c.members.push_back(member("N", "INT"));
    c.methods.push_back(method(p, "Incr", {}, "N := N + 1;"));
    c.methods.push_back(method(p, "Double", "INT", "Double := SELF.N * 2;"));
    return p;
}

// T_Forme { Cote } VIRTUAL Aire():REAL = Cote*Cote ; T_Cube EXTENDS T_Forme OVERRIDE Aire = SUPER*6.
Project formeCubeProject() {
    Project p;
    HmiType& f = addType(p, "T_Forme");
    f.members.push_back(member("Cote", "REAL"));
    f.methods.push_back(method(p, "Aire", "REAL", "Aire := SELF.Cote * SELF.Cote;", Visibility::Public, true));
    HmiType& cube = addType(p, "T_Cube");
    cube.base = "T_Forme";
    cube.methods.push_back(method(p, "Aire", "REAL", "Aire := SUPER.Aire() * 6.0;", Visibility::Public, false));
    return p;
}
} // namespace

void moteur() {
    std::printf("-- moteur : SELF, procedure/fonction, variable/case/membre, heritage, dispatch\n");
    std::string err;

    // 1) procedure qui ecrit SELF, sur une variable ; 2) fonction qui lit SELF.
    {
        Project p = counterProject();
        addVar(p, "c", "T_Compteur");
        addVar(p, "R", "INT");
        const std::string r = run(p, "c.Incr();\nc.Incr();\nc.Incr();\nR := c.N;", &err);
        check(r == "3", "une procedure ecrit SELF (c.Incr() x3 -> c.N = 3) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }
    {
        Project p = counterProject();
        addVar(p, "c", "T_Compteur", "");
        addVar(p, "R", "INT");
        const std::string r = run(p, "c.Incr();\nc.Incr();\nR := c.Double();", &err);
        check(r == "4", "une fonction lit SELF (c.Double() = 2*N = 4) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }

    // 3) appel sur une case de tableau.
    {
        Project p = counterProject();
        addVar(p, "T", "ARRAY[0..2] OF T_Compteur");
        addVar(p, "R", "INT");
        const std::string r = run(p, "T[1].Incr();\nT[1].Incr();\nR := T[1].N;", &err);
        check(r == "2", "appel sur une case de tableau (T[1].Incr() x2 -> 2) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }

    // 4) appel sur un membre compose.
    {
        Project p = counterProject();
        HmiType& box = addType(p, "T_Boite");
        box.members.push_back(member("Compteur", "T_Compteur"));
        addVar(p, "b", "T_Boite");
        addVar(p, "R", "INT");
        const std::string r = run(p, "b.Compteur.Incr();\nb.Compteur.Incr();\nR := b.Compteur.N;", &err);
        check(r == "2", "appel sur un membre compose (b.Compteur.Incr() x2 -> 2) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }

    // 5) EXTENDS : la methode heritee tourne sur le sous-type ; SELF voit les membres du parent.
    {
        Project p = counterProject();
        HmiType& d = addType(p, "T_CompteurNomme");
        d.base = "T_Compteur";
        d.members.push_back(member("Nom", "STRING"));
        addVar(p, "d", "T_CompteurNomme");
        addVar(p, "R", "INT");
        const std::string r = run(p, "d.Incr();\nd.Incr();\nd.Incr();\nR := d.Double();", &err);
        check(r == "6", "methode heritee sur un sous-type (d.Incr() x3, d.Double() = 6) [" + r + (err.empty() ? "" : " err=" + err) + "]");
    }

    // 6) OVERRIDE + SUPER + dispatch virtuel a travers une REF_TO (le scenario documente :
    //    une liste heterogene de REF_TO, chaque e^.Aire() part sur le type REEL).
    {
        Project p = formeCubeProject();
        addVar(p, "carre", "T_Forme", "");
        addVar(p, "cube", "T_Cube", "");
        addVar(p, "L", "LIST OF REF_TO T_Forme");
        addVar(p, "R", "REAL");
        // carre.Cote=2 -> Aire=4 ; cube.Cote=2 -> Aire = SUPER(4)*6 = 24 ; total 28.
        const std::string body =
            "carre.Cote := 2.0;\ncube.Cote := 2.0;\n"
            "LIST_ADD(L, REF(carre));\nLIST_ADD(L, REF(cube));\n"
            "R := 0.0;\nFOR EACH e IN L DO\n  R := R + e^.Aire();\nEND_FOR;";
        const std::string r = run(p, body, &err);
        check(r == "28", "dispatch virtuel + SUPER sur une liste heterogene : 4 (carre) + 24 (cube) = 28 ["
              + r + (err.empty() ? "" : " err=" + err) + "]");
        // le carre seul (type reel T_Forme) : la virtuelle de base.
        Project p1 = formeCubeProject();
        addVar(p1, "carre", "T_Forme", "");
        addVar(p1, "L", "LIST OF REF_TO T_Forme");
        addVar(p1, "R", "REAL");
        const std::string r1 = run(p1, "carre.Cote := 3.0;\nLIST_ADD(L, REF(carre));\nR := L[0]^.Aire();", &err);
        check(r1 == "9", "dispatch : un T_Forme donne sa virtuelle Aire = 9 [" + r1 + (err.empty() ? "" : " err=" + err) + "]");
    }

    // 7) DYNAMIC_CAST<T>(e^) : rend une reference si le type reel va, NULL sinon.
    {
        Project p = formeCubeProject();
        addVar(p, "cube", "T_Cube", "");
        addVar(p, "carre", "T_Forme", "");
        addVar(p, "L", "LIST OF REF_TO T_Forme");
        addVar(p, "R", "REAL");
        // cube^ EST un T_Cube : DYNAMIC_CAST<T_Cube> reussit ; carre^ n'en est pas un : NULL.
        const std::string body =
            "cube.Cote := 5.0;\ncarre.Cote := 1.0;\n"
            "LIST_ADD(L, REF(cube));\nLIST_ADD(L, REF(carre));\n"
            "R := 0.0;\n"
            "VAR c : REF_TO T_Cube; END_VAR\n"
            "FOR EACH e IN L DO\n"
            "  c := DYNAMIC_CAST<T_Cube>(e^);\n"
            "  IF c <> NULL THEN R := R + c^.Cote; END_IF;\n"
            "END_FOR;";
        const std::string r = run(p, body, &err);
        check(r == "5", "DYNAMIC_CAST<T_Cube> : seul le cube passe (Cote 5), le carre donne NULL ["
              + r + (err.empty() ? "" : " err=" + err) + "]");
    }
}
