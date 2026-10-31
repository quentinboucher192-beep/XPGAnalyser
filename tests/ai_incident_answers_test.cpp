// =============================================================================
//  tests/ai_incident_answers_test.cpp - 1.20.0 : l'Assistant IA diagnostique les incidents (AiIncidentAnswers)
// -----------------------------------------------------------------------------
//  Sans ecran, sans disque, sans reseau : des dossiers d'incident construits a la
//  main (des Snapshot remplis comme le detecteur les remplit) -
//    - trois incidents de la pompe 2 (deux de la meme alarme), un equipement commun
//      (Station_Modbus), une variable indirecte jamais relevee ;
//    - une perte de communication (Automate_Cuves) avant une alarme de mesure qui lit
//      ses variables, et une autre alarme de mesure sans lien avec elle ;
//    - cinq alarmes apparues a moins d'une seconde (une sixieme, 20 s plus tard, n'en est pas).
//  Chaque question de la demande : la bonne alarme, la chaine par profondeur, << non
//  disponible >> (jamais une valeur supposee), les simultanees, l'equipement commun,
//  << peut expliquer >> et jamais << a cause >>, rien d'invente (chaque nom cite en `code`
//  est dans les donnees ou dans la demande), aucun incident, le contexte de l'IA distante.
// =============================================================================
#include "../src/app/ai/AiIncidentAnswers.hpp"
#include "../src/hmi/HmiModel.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace inc = hmi::incident;
namespace dg = app::ai::diagnosis;
using app::ai::incidents::Item;
using app::ai::incidents::State;
using Q = dg::Question;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(const std::string& text, const std::string& what) { return text.find(what) != std::string::npos; }

inc::Link link(const char* kind, const char* name, const char* relation, int depth = 1, inc::Certainty c = inc::Certainty::Fact,
               const char* where = "", const char* detail = "") {
    inc::Link l;
    l.kind = kind;
    l.name = name;
    l.relation = relation;
    l.depth = depth;
    l.certainty = c;
    l.where = where;
    l.detail = detail;
    return l;
}
inc::Event event(inc::EventKind k, const char* name, double t, const char* at = "") {
    inc::Event e;
    e.kind = k;
    e.name = name;
    e.t = t;
    e.at = at;
    return e;
}
inc::Hypothesis hypo(const char* text, inc::Certainty c, const char* evidence, const char* check) {
    inc::Hypothesis h;
    h.text = text;
    h.certainty = c;
    h.evidence.push_back(evidence);
    h.check.push_back(check);
    return h;
}

Item item(const char* id, const char* alarm, const char* at, double t, State st = State::Pending) {
    Item it;
    it.readable = true;
    it.state = st;
    it.snapshot.id = id;
    it.snapshot.createdAt = at;
    it.snapshot.trigger = event(inc::EventKind::Appear, alarm, t, at);
    it.snapshot.alarm.name = alarm;
    it.snapshot.alarm.known = true;
    it.name = std::string(at).substr(0, 10) + "-" + alarm + "-" + id;
    it.folder = "/projet/ihm/ressources/enregistrements/en attente/" + it.name;
    return it;
}

// La pompe 2 : deux POMPE_2_PRESSION_HAUTE (la seconde validee), un POMPE_2_DEFAUT_MARCHE ; Station_Modbus partout.
Item pressure(const char* id, const char* at, double t, const char* value, State st) {
    Item it = item(id, "POMPE_2_PRESSION_HAUTE", at, t, st);
    auto& s = it.snapshot;
    s.alarm.condition = "Pompe_2.Pression > 8.0";
    s.alarm.priority = 2;
    s.conditionVariables = {"Pompe_2.Pression"};
    s.values.push_back({"Pompe_2.Pression", value, "bonne", true});
    s.chain.push_back(link("variable", "Pompe_2.Pression", "lue par la condition", 1, inc::Certainty::Fact, "alarme POMPE_2_PRESSION_HAUTE (condition)"));
    s.chain.push_back(link("structure", "T_Pompe", "type de Pompe_2"));
    s.chain.push_back(link("equipement", "Station_Modbus", "source de Pompe_2", 1, inc::Certainty::Fact, "", "Modbus TCP/IP 192.168.1.30:502 (esclave 1)"));
    s.chain.push_back(link("adresse", "Pompe_2 @ 40011", "adresse de communication", 1, inc::Certainty::Fact, "", "Station_Modbus"));
    s.chain.push_back(link("script", "Calcul_P2", "\xC3\xA9" "crit Pompe_2.Pression", 1, inc::Certainty::Fact, "script Calcul_P2, ligne 3"));
    s.chain.push_back(link("objet", "Mesure_P2 (Vue_Pompes)", "affiche Pompe_2.Pression", 1, inc::Certainty::Fact, "Vue_Pompes . Mesure_P2 . value"));
    s.chain.push_back(link("vue", "Vue_Pompes", "montre Pompe_2"));
    s.chain.push_back(link("variable", "Consigne_P2", "lue par Calcul_P2 (qui \xC3\xA9" "crit la condition)", 2, inc::Certainty::Indirect,
                           "script Calcul_P2, ligne 2"));
    s.hypotheses.push_back(hypo("La mesure Pompe_2 vient de Station_Modbus (40011) : un capteur ou une mesure incorrecte peut d\xC3\xA9" "clencher l'alarme.",
                                inc::Certainty::Hypothesis, "la condition lit Pompe_2", "le capteur et la valeur brute de Pompe_2 sur Station_Modbus"));
    s.hypotheses.push_back(hypo("Le traitement Calcul_P2 \xC3\xA9" "crit une variable de la condition : un calcul peut l'influencer.",
                                inc::Certainty::Hypothesis, "Calcul_P2 \xC3\xA9" "crit une variable lue par la condition", "le code de Calcul_P2"));
    s.summary = "Apparition de \xC2\xAB POMPE_2_PRESSION_HAUTE \xC2\xBB ; la condition lit 1 variable(s) de 1 \xC3\xA9quipement(s).";
    s.timeline.push_back(s.trigger);
    return it;
}

std::vector<Item> dataset() {
    std::vector<Item> v;
    // 1. la pompe 2
    v.push_back(pressure("p1", "2026-10-30 08:00:00.000", 10.0, "8.4", State::Validated));
    v.push_back(pressure("p2", "2026-10-31 09:00:00.000", 20.0, "8.9", State::Pending));
    v.back().snapshot.previous.push_back({"p1", "2026-10-30 08:00:00.000", "dossier-p1", "valid\xC3\xA9", "premier"});
    v.back().snapshot.previous.push_back({"p0", "2026-10-29 07:00:00.000", "dossier-p0", "valid\xC3\xA9", "un plus ancien, supprim\xC3\xA9 depuis"});
    {
        Item it = item("p3", "POMPE_2_DEFAUT_MARCHE", "2026-10-31 10:00:00.000", 30.0);
        auto& s = it.snapshot;
        s.alarm.condition = "Pompe_2.Defaut";
        s.conditionVariables = {"Pompe_2.Defaut"};
        s.values.push_back({"Pompe_2.Defaut", "", "", false});   // non disponible
        s.chain.push_back(link("variable", "Pompe_2.Defaut", "lue par la condition"));
        s.chain.push_back(link("equipement", "Station_Modbus", "source de Pompe_2", 1, inc::Certainty::Fact, "", "Modbus TCP/IP"));
        s.missing.push_back("la valeur de Pompe_2.Defaut au moment du d\xC3\xA9" "faut (non disponible)");
        s.summary = "Apparition de \xC2\xAB POMPE_2_DEFAUT_MARCHE \xC2\xBB.";
        v.push_back(it);
    }
    // 2. la communication : Automate_Cuves perd la communication 2 s avant NIVEAU_CUVE_1_HAUT (qui lit ses variables)
    {
        Item it = item("c0", "Automate_Cuves", "2026-10-31 11:00:03.000", 103.0);
        it.snapshot.trigger.kind = inc::EventKind::CommLost;
        it.snapshot.alarm.known = false;
        it.snapshot.alarm.category = "Communication";
        v.push_back(it);
    }
    {
        Item it = item("c1", "NIVEAU_CUVE_1_HAUT", "2026-10-31 11:00:05.000", 105.0);
        auto& s = it.snapshot;
        s.alarm.condition = "Cuve_1.Niveau > 90.0";
        s.conditionVariables = {"Cuve_1.Niveau"};
        s.values.push_back({"Cuve_1.Niveau", "91.2", "incertaine", true});
        s.chain.push_back(link("variable", "Cuve_1.Niveau", "lue par la condition"));
        s.chain.push_back(link("equipement", "Automate_Cuves", "source de Cuve_1"));
        s.timeline.push_back(event(inc::EventKind::CommLost, "Automate_Cuves", 103.0, "2026-10-31 11:00:03.000"));
        s.timeline.push_back(s.trigger);
        inc::Related r;
        r.name = "NIVEAU_CUVE_2_BAS";
        r.kind = inc::Related::Kind::Secondary;
        r.why = "m\xC3\xAAme \xC3\xA9quipement Automate_Cuves ; apr\xC3\xA8s la perte de communication de Automate_Cuves";
        s.related.push_back(r);
        s.hypotheses.push_back(hypo("Un d\xC3\xA9" "faut de communication avec Automate_Cuves peut expliquer l'alarme : ses mesures ont pu \xC3\xAAtre fig\xC3\xA9" "es ou fausses.",
                                    inc::Certainty::Indirect, "perte de communication de Automate_Cuves 2,0 s avant l'alarme",
                                    "la liaison avec Automate_Cuves"));
        v.push_back(it);
    }
    {
        Item it = item("c2", "TEMPERATURE_FOUR", "2026-10-31 11:30:00.000", 1900.0);
        auto& s = it.snapshot;
        s.alarm.condition = "Four.Temperature > 250.0";
        s.conditionVariables = {"Four.Temperature"};
        s.values.push_back({"Four.Temperature", "262", "bonne", true});
        s.chain.push_back(link("variable", "Four.Temperature", "lue par la condition"));
        s.chain.push_back(link("equipement", "Automate_Four", "source de Four"));
        s.timeline.push_back(event(inc::EventKind::CommLost, "Automate_Cuves", 1850.0));
        s.timeline.push_back(s.trigger);
        v.push_back(it);
    }
    // 3. cinq alarmes en meme temps (une sixieme 20 s apres), et le dossier d'une d'elles
    {
        Item it = item("s1", "ARRET_URGENCE", "2026-10-31 12:00:00.000", 500.0);
        auto& s = it.snapshot;
        s.alarm.condition = "Ligne.Arret";
        s.conditionVariables = {"Ligne.Arret"};
        s.values.push_back({"Ligne.Arret", "TRUE", "bonne", true});
        s.chain.push_back(link("variable", "Ligne.Arret", "lue par la condition"));
        s.chain.push_back(link("equipement", "Automate_Ligne", "source de Ligne"));
        s.timeline.push_back(event(inc::EventKind::Appear, "PRESSION_AIR_BASSE", 499.5));
        s.timeline.push_back(s.trigger);
        s.timeline.push_back(event(inc::EventKind::Appear, "DEFAUT_CONVOYEUR_1", 500.2));
        s.timeline.push_back(event(inc::EventKind::Appear, "DEFAUT_CONVOYEUR_2", 500.4));
        s.timeline.push_back(event(inc::EventKind::Appear, "DEFAUT_VARIATEUR", 500.6));
        s.timeline.push_back(event(inc::EventKind::Appear, "ALARME_LOINTAINE", 520.0));
        inc::Related r;
        r.name = "DEFAUT_CONVOYEUR_1";
        r.kind = inc::Related::Kind::Shared;
        r.why = "m\xC3\xAAme variable Ligne";
        s.related.push_back(r);
        v.push_back(it);
    }
    {
        Item it = item("s2", "DEFAUT_CONVOYEUR_1", "2026-10-31 12:00:00.200", 500.2);
        auto& s = it.snapshot;
        s.alarm.condition = "Ligne.Convoyeur_1.Defaut";
        s.conditionVariables = {"Ligne.Convoyeur_1.Defaut"};
        s.chain.push_back(link("variable", "Ligne.Convoyeur_1.Defaut", "lue par la condition"));
        s.chain.push_back(link("equipement", "Automate_Ligne", "source de Ligne"));
        v.push_back(it);
    }
    return v;
}

// Tout ce que les dossiers disent : un nom cite en `code` doit en venir (ou de la demande).
std::string corpus(const std::vector<Item>& items) {
    std::string c;
    for (const auto& it : items) {
        const auto& s = it.snapshot;
        c += it.name + "\n" + s.trigger.name + "\n" + s.alarm.name + "\n" + s.alarm.condition + "\n";
        for (const auto& x : s.conditionVariables) c += x + "\n";
        for (const auto& l : s.chain) c += l.name + "\n" + l.relation + "\n" + l.where + "\n" + l.detail + "\n";
        for (const auto& x : s.values) c += x.path + "\n" + x.value + "\n";
        for (const auto& r : s.related) c += r.name + "\n" + r.why + "\n";
        for (const auto& e : s.timeline) c += e.name + "\n";
    }
    return c;
}

// Les morceaux `code` de la reponse qui ne viennent ni des donnees ni de la demande (vide : rien d'invente).
std::string invented(const std::string& answer, const std::string& data, const std::string& request) {
    std::string bad;
    std::size_t at = 0;
    while ((at = answer.find('`', at)) != std::string::npos) {
        const std::size_t end = answer.find('`', at + 1);
        if (end == std::string::npos) break;
        const std::string c = answer.substr(at + 1, end - at - 1);
        at = end + 1;
        if (c.empty() || has(data, c) || has(request, c) || c == "ihm/ressources/enregistrements/en attente/") continue;
        bad += "[" + c + "] ";
    }
    return bad;
}

// Jamais une cause affirmee.
bool affirmsCause(const std::string& t) {
    return has(t, "\xC3\xA0 cause") || has(t, "a caus\xC3\xA9") || has(t, "a provoqu\xC3\xA9") || has(t, "est la cause") || has(t, "est caus\xC3\xA9");
}

bool hasRef(const dg::Answer& a, const char* kind, const char* name) {
    for (const auto& r : a.refs)
        if (r.kind == kind && r.name == name) return true;
    return false;
}

} // namespace

int main() {
    std::printf("ai_incident_answers_test : l'Assistant IA diagnostique les incidents\n");
    const std::vector<Item> items = dataset();
    const std::string data = corpus(items);
    const auto ask = [&](const std::string& request, const hmi::Project* project = nullptr) {
        dg::Answer a = dg::answer(request, items, project);
        if (std::getenv("XPG_INCIDENTS_DUMP")) {   // pour relire les reponses
            std::printf("\n>>> %s\n%s", request.c_str(), a.text.c_str());
            for (const auto& s : a.suggestions) std::printf("  [suggestion] %s\n", s.c_str());
        }
        const std::string bad = invented(a.text, data, request);
        check(bad.empty(), "rien d'invent\xC3\xA9 pour \xC2\xAB " + request + " \xC2\xBB : " + bad);
        check(!affirmsCause(a.text), "jamais une cause affirm\xC3\xA9" "e pour \xC2\xAB " + request + " \xC2\xBB");
        check(!a.suggestions.empty(), "des questions suivantes pour \xC2\xAB " + request + " \xC2\xBB");
        for (const auto& s : a.suggestions) {
            std::size_t n = 0;
            for (const char c : s) n += (static_cast<unsigned char>(c) & 0xC0) != 0x80;
            check(n <= 45, "une suggestion tient sur une ligne (45 caract\xC3\xA8res, l'IA int\xC3\xA9gr\xC3\xA9" "e retire les autres) : " + s);
            check(dg::isIncidentQuestion(s) || s == "V\xC3\xA9rifie tout mon projet" || s == "Que sais-tu faire ?",
                  "une suggestion est comprise : " + s);
        }
        return a;
    };
    const std::string causality = "L'ordre d'apparition n'est pas une preuve de causalit\xC3\xA9";

    // ---- 1. reconnaitre les questions (et seulement elles) --------------------------------------------------------
    {
        const std::pair<const char*, Q> yes[] = {
            {"Pourquoi cette alarme est-elle apparue ?", Q::Why},
            {"Fais-moi un diagnostic complet de cet incident", Q::Diagnosis},
            {"Retrouve toute la cha\xC3\xAEne de d\xC3\xA9pendances de cette alarme", Q::Chain},
            {"Quelles variables peuvent \xC3\xAAtre \xC3\xA0 l\xE2\x80\x99origine de ce d\xC3\xA9" "faut ?", Q::Origins},
            {"Pourquoi cinq alarmes sont-elles apparues en m\xC3\xAAme temps ?", Q::Simultaneous},
            {"Est-ce que cette alarme est d\xC3\xA9j\xC3\xA0 apparue dans le pass\xC3\xA9 ?", Q::History},
            {"Compare les trois derniers incidents de cette pompe", Q::Compare},
            {"Quel \xC3\xA9quipement est commun \xC3\xA0 ces diff\xC3\xA9rents d\xC3\xA9" "fauts ?", Q::CommonEquipment},
            {"Montre-moi les scripts qui peuvent influencer cette variable", Q::Scripts},
            {"Est-ce que les d\xC3\xA9" "fauts de communication expliquent les alarmes de mesure ?", Q::Communication},
            {"Quels sont les incidents en attente ?", Q::List},
            {"POURQUOI CETTE ALARME EST APPARUE", Q::Why},
        };
        for (const auto& [phrase, q] : yes) {
            check(dg::isIncidentQuestion(phrase), std::string("une question d'incident : ") + phrase);
            check(dg::classify(phrase) == q, std::string("la bonne sorte : ") + phrase + " (" + std::to_string(static_cast<int>(dg::classify(phrase))) + ")");
        }
        const char* no[] = {
            "Quelles alarmes concernent la pompe 2 ?",
            "Ajoute une alarme de d\xC3\xA9" "faut sur la pompe 2",
            "Cr\xC3\xA9" "e une vue de diagnostic",
            "Montre-moi les scripts qui utilisent Niveau_Cuve_1",
            "Anime le d\xC3\xA9" "faut de la pompe en rouge",
            "Ajoute une alarme de temp\xC3\xA9rature haute sur les quatre cuves",
            "V\xC3\xA9rifie tout mon projet",
            "Ajoute un bouton",
        };
        for (const char* phrase : no) check(!dg::isIncidentQuestion(phrase), std::string("pas une question d'incident : ") + phrase);
    }

    // ---- 2. aucun incident ----------------------------------------------------------------------------------------
    {
        const auto a = dg::answer("Pourquoi cette alarme est-elle apparue ?", {}, nullptr);
        check(has(a.text, "aucun dossier d'incident") && has(a.text, "simulation") && has(a.text, "surveillance des incidents"),
              "aucun incident : le dire, et comment en obtenir");
        check(!has(a.text, "POMPE") && a.refs.empty(), "aucun incident : rien d'autre");
        check(has(dg::contextBlock("Pourquoi cette alarme est-elle apparue ?", {}), "Aucun dossier d'incident"), "aucun incident : le contexte distant le dit");
    }

    // ---- 3. pourquoi (l'alarme nommee, la derniere de ses incidents) -----------------------------------------------
    {
        const std::string q = "Pourquoi l'alarme POMPE_2_PRESSION_HAUTE est-elle apparue ?";
        const auto a = ask(q);
        check(a.question == Q::Why, "pourquoi : la sorte");
        check(has(a.text, "`POMPE_2_PRESSION_HAUTE`") && has(a.text, "2026-10-31 09:00:00") && has(a.text, "le dernier des 2"),
              "pourquoi : le dernier incident de l'alarme nomm\xC3\xA9" "e");
        check(has(a.text, "`Pompe_2.Pression` = 8.9 (bonne)") && !has(a.text, "8.4"), "pourquoi : sa valeur au moment du d\xC3\xA9" "faut (celle de cet incident)");
        check(has(a.text, "1. [hypoth\xC3\xA8se]") && has(a.text, "2. [hypoth\xC3\xA8se]") && has(a.text, "appui : la condition lit Pompe_2"),
              "pourquoi : les pistes class\xC3\xA9" "es, leur certitude et leur appui");
        check(has(a.text, causality), "pourquoi : l'ordre d'apparition n'est pas une preuve");
        check(hasRef(a, "alarme", "POMPE_2_PRESSION_HAUTE") && hasRef(a, "equipement", "Station_Modbus") && hasRef(a, "script", "Calcul_P2")
                  && hasRef(a, "variable", "Pompe_2"),
              "pourquoi : les \xC3\xA9l\xC3\xA9ments cit\xC3\xA9s (liens)");
        // sans alarme nommee : le dernier incident
        const auto b = ask("Pourquoi cette alarme est-elle apparue ?");
        check(has(b.text, "`DEFAUT_CONVOYEUR_1`") && has(b.text, causality), "pourquoi cette alarme : le dernier incident (" + b.text.substr(0, 80) + ")");
        check(has(b.text, "**Aucune piste class\xC3\xA9" "e**") && has(b.text, "Sa condition lit des variables de `Automate_Ligne` (fait v\xC3\xA9rifi\xC3\xA9)"),
              "pourquoi, sans piste : rien de plus que le dossier (son \xC3\xA9quipement, un fait)");
        // une alarme qui n'a aucun incident : le dire, sans rien diagnostiquer
        const auto c = ask("Pourquoi l'alarme POMPE_9_DEFAUT est-elle apparue ?");
        check(has(c.text, "aucun incident") && has(c.text, "`POMPE_9_DEFAUT`") && !has(c.text, "pistes"), "une alarme sans incident : dite, rien d'invent\xC3\xA9");
    }

    // ---- 4. le diagnostic complet (la fiche) -----------------------------------------------------------------------
    {
        const auto a = ask("Fais-moi un diagnostic complet de l'incident POMPE_2_DEFAUT_MARCHE");
        check(a.question == Q::Diagnosis, "diagnostic : la sorte");
        check(has(a.text, "## Incident : POMPE_2_DEFAUT_MARCHE") && has(a.text, "**1. R\xC3\xA9sum\xC3\xA9**") && has(a.text, "**10. Niveau de certitude**"),
              "diagnostic : la fiche en dix sections (rendue pour le panneau)");
        check(!has(a.text, "### "), "diagnostic : aucun intertitre que le panneau ne rend pas");
        check(has(a.text, "Pompe_2.Defaut = non disponible"), "diagnostic : la valeur absente est dite non disponible");
        check(has(a.text, causality), "diagnostic : l'ordre d'apparition n'est pas une preuve");
    }

    // ---- 5. la chaine de dependances -------------------------------------------------------------------------------
    {
        const auto a = ask("Retrouve toute la cha\xC3\xAEne de d\xC3\xA9pendances de l'alarme POMPE_2_PRESSION_HAUTE");
        check(a.question == Q::Chain, "cha\xC3\xAEne : la sorte");
        const auto p1 = a.text.find("**Profondeur 1"), p2 = a.text.find("**Profondeur 2");
        check(p1 != std::string::npos && p2 != std::string::npos && p1 < p2, "cha\xC3\xAEne : par profondeur, directe puis indirecte");
        check(has(a.text, "[fait v\xC3\xA9rifi\xC3\xA9] equipement `Station_Modbus`") && has(a.text, "[relation indirecte] variable `Consigne_P2`")
                  && a.text.find("Consigne_P2") > p2,
              "cha\xC3\xAEne : chaque maillon avec sa certitude, l'indirect en profondeur 2");
        check(has(a.text, "script `Calcul_P2`") && has(a.text, "script Calcul_P2, ligne 3"), "cha\xC3\xAEne : le script et o\xC3\xB9");
        check(has(a.text, "8 maillons") && has(a.text, "7 fait(s) v\xC3\xA9rifi\xC3\xA9(s), 1 relation(s) indirecte(s)"), "cha\xC3\xAEne : le compte");
        check(hasRef(a, "objet", "Mesure_P2") && hasRef(a, "vue", "Vue_Pompes") && hasRef(a, "type", "T_Pompe"), "cha\xC3\xAEne : les liens (objet, vue, type)");
    }

    // ---- 6. les variables a l'origine (valeurs : jamais supposees) ---------------------------------------------------
    {
        const auto a = ask("Quelles variables peuvent \xC3\xAAtre \xC3\xA0 l'origine du d\xC3\xA9" "faut POMPE_2_PRESSION_HAUTE ?");
        check(a.question == Q::Origins, "origine : la sorte");
        check(has(a.text, "`Pompe_2.Pression` = 8.9 (bonne)") && has(a.text, "vient de l'\xC3\xA9quipement `Station_Modbus` (adresse 40011)"),
              "origine : la variable directe, sa valeur, son \xC3\xA9quipement");
        check(has(a.text, "`Consigne_P2` = non disponible") && has(a.text, "**Indirectes**"), "origine : l'indirecte, sa valeur NON DISPONIBLE (jamais invent\xC3\xA9" "e)");
        const auto b = ask("Quelles variables peuvent \xC3\xAAtre \xC3\xA0 l'origine du d\xC3\xA9" "faut POMPE_2_DEFAUT_MARCHE ?");
        check(has(b.text, "`Pompe_2.Defaut` = non disponible"), "origine : une valeur absente du dossier est dite non disponible");
    }

    // ---- 7. cinq alarmes en meme temps -----------------------------------------------------------------------------
    {
        const auto a = ask("Pourquoi cinq alarmes sont-elles apparues en m\xC3\xAAme temps ?");
        check(a.question == Q::Simultaneous, "en m\xC3\xAAme temps : la sorte");
        check(has(a.text, "**5 alarmes sont apparues \xC3\xA0 moins d'une seconde"), "en m\xC3\xAAme temps : les cinq (" + a.text.substr(0, 120) + ")");
        for (const char* n : {"ARRET_URGENCE", "PRESSION_AIR_BASSE", "DEFAUT_CONVOYEUR_1", "DEFAUT_CONVOYEUR_2", "DEFAUT_VARIATEUR"})
            check(has(a.text, std::string("`") + n + "`"), std::string("en m\xC3\xAAme temps : ") + n);
        check(!has(a.text, "ALARME_LOINTAINE"), "en m\xC3\xAAme temps : pas celle de 20 s plus tard");
        check(has(a.text, "0,5 s avant") && has(a.text, "0,6 s apr\xC3\xA8s"), "en m\xC3\xAAme temps : les \xC3\xA9" "carts");
        check(has(a.text, "d\xC3\xA9pendance technique commune") && has(a.text, "m\xC3\xAAme variable Ligne"), "en m\xC3\xAAme temps : la d\xC3\xA9pendance commune");
        check(has(a.text, "**\xC3\x89quipements communs**") && has(a.text, "`Automate_Ligne` : `ARRET_URGENCE`, `DEFAUT_CONVOYEUR_1`"),
              "en m\xC3\xAAme temps : l'\xC3\xA9quipement commun");
        check(has(a.text, "3 alarmes n'ont pas de dossier"), "en m\xC3\xAAme temps : celles sans dossier sont dites");
        check(has(a.text, "corr\xC3\xA9lation, pas une causalit\xC3\xA9"), "en m\xC3\xAAme temps : corr\xC3\xA9lation n'est pas causalit\xC3\xA9");
    }

    // ---- 8. deja apparue -------------------------------------------------------------------------------------------
    {
        const auto a = ask("Est-ce que l'alarme POMPE_2_PRESSION_HAUTE est d\xC3\xA9j\xC3\xA0 apparue dans le pass\xC3\xA9 ?");
        check(a.question == Q::History, "historique : la sorte");
        check(has(a.text, "**Oui**") && has(a.text, "**3 incidents**") && has(a.text, "2026-10-31 09:00:00") && has(a.text, "2026-10-30 08:00:00"),
              "historique : ses incidents (" + a.text.substr(0, 100) + ")");
        check(has(a.text, "valid\xC3\xA9") && has(a.text, "en attente"), "historique : leur \xC3\xA9tat");
        check(has(a.text, "2026-10-29 07:00:00") && has(a.text, "supprim\xC3\xA9 depuis"), "historique : l'historique not\xC3\xA9 au moment du d\xC3\xA9" "faut (previous)");
        const auto b = ask("Est-ce que cette alarme est d\xC3\xA9j\xC3\xA0 apparue dans le pass\xC3\xA9 ?");
        check(has(b.text, "**Non**") && has(b.text, "`DEFAUT_CONVOYEUR_1`"), "historique : cette alarme (la derni\xC3\xA8re), une seule fois");
    }

    // ---- 9. comparer les trois derniers incidents de cette pompe ---------------------------------------------------
    {
        for (const char* q : {"Compare les trois derniers incidents de cette pompe", "Compare les trois derniers incidents de la pompe 2"}) {
            const auto a = ask(q);
            check(a.question == Q::Compare, std::string("comparer : la sorte (") + q + ")");
            check(has(a.text, "**Les 3 derniers incidents** de `Pompe_2`") && has(a.text, "1. **2026-10-31 10:00:00**") && has(a.text, "2. **2026-10-31 09:00:00**")
                      && has(a.text, "3. **2026-10-30 08:00:00**"),
                  std::string("comparer : les trois, du plus r\xC3\xA9" "cent au plus ancien (") + q + ")");
            check(has(a.text, "`Pompe_2.Pression` : non lue \xE2\x86\x92 8.9 (bonne) \xE2\x86\x92 8.4 (bonne)")
                      && has(a.text, "`Pompe_2.Defaut` : non disponible \xE2\x86\x92 non lue \xE2\x86\x92 non lue"),
                  std::string("comparer : les valeurs d'un incident \xC3\xA0 l'autre, sans en supposer (") + q + ")");
            check(has(a.text, "\xC3\xA9quipement `Station_Modbus` (3 sur 3)"), std::string("comparer : l'\xC3\xA9quipement commun (") + q + ")");
            check(has(a.text, "certitude : hypoth\xC3\xA8se") && has(a.text, "pistes : aucune"), std::string("comparer : pistes et certitude (") + q + ")");
            check(!has(a.text, "NIVEAU_CUVE") && !has(a.text, "ARRET_URGENCE"), std::string("comparer : seulement la pompe 2 (") + q + ")");
        }
    }

    // ---- 10. l'equipement commun -----------------------------------------------------------------------------------
    {
        const auto a = ask("Quel \xC3\xA9quipement est commun aux d\xC3\xA9" "fauts de la pompe 2 ?");
        check(a.question == Q::CommonEquipment, "\xC3\xA9quipement commun : la sorte");
        check(has(a.text, "**Commun \xC3\xA0 tous** : `Station_Modbus`.") && has(a.text, "dans **3 sur 3**"), "\xC3\xA9quipement commun : Station_Modbus, 3 sur 3");
        const auto b = ask("Quel \xC3\xA9quipement est commun \xC3\xA0 ces diff\xC3\xA9rents d\xC3\xA9" "fauts ?");
        check(has(b.text, "`Automate_Cuves` : dans **2 sur 8** (`NIVEAU_CUVE_1_HAUT`, perte de communication)"), "\xC3\xA9quipement commun : un incident de communication se dit tel");
        check(has(b.text, "**Aucun \xC3\xA9quipement n'est commun \xC3\xA0 tous**") && has(b.text, "le plus fr\xC3\xA9quent : `Station_Modbus` (3 sur 8)"),
              "\xC3\xA9quipement commun : sur tous les d\xC3\xA9" "fauts, aucun (le plus fr\xC3\xA9quent dit)");
        check(has(b.text, "pas la preuve d'une cause commune"), "\xC3\xA9quipement commun : pas une cause");
    }

    // ---- 11. les scripts qui peuvent influencer une variable -------------------------------------------------------
    {
        const auto a = ask("Montre-moi les scripts qui peuvent influencer Pompe_2.Pression");
        check(a.question == Q::Scripts, "scripts : la sorte");
        check(has(a.text, "script `Calcul_P2` **l'\xC3\xA9" "crit**") && has(a.text, "script Calcul_P2, ligne 3"), "scripts : celui qui l'\xC3\xA9" "crit (les dossiers)");
        check(has(a.text, "`Station_Modbus`"), "scripts : son \xC3\xA9quipement l'influence aussi");
        check(!a.suggestions.empty() && has(a.suggestions.front(), "POMPE_2_PRESSION_HAUTE") && dg::classify(a.suggestions.front()) == Q::Why,
              "scripts : les questions suivantes sur l'incident qui la cite");
        const auto b = ask("Montre-moi les scripts qui peuvent influencer Consigne_P2");
        check(has(b.text, "`Calcul_P2` la lit et \xC3\xA9" "crit la condition de `POMPE_2_PRESSION_HAUTE`") && has(b.text, "[relation indirecte]"),
              "scripts : une variable indirecte (relation indirecte)");
        const auto c = ask("Montre-moi les scripts qui peuvent influencer cette variable");
        check(has(c.text, "`Ligne.Convoyeur_1.Defaut`") && has(c.text, "**Aucun script ni fonction**"), "scripts : cette variable (le dernier incident), aucun");
        // le projet ouvert : le graphe des relations
        hmi::Project p;
        hmi::Variable v;
        v.id = p.allocate();
        v.name = "Niveau_Cuve_1";
        v.type = "REAL";
        v.equipment = "Automate_Cuves";
        v.address = "40021";
        p.programs.variables.push_back(v);
        hmi::Script lissage;
        lissage.id = p.allocate();
        lissage.name = "Lissage";
        lissage.event = "Cyclique";
        lissage.body = "Niveau_Cuve_1 := Niveau_Cuve_1 * 0.9;\n";
        p.programs.scripts.push_back(lissage);
        hmi::Script affiche;
        affiche.id = p.allocate();
        affiche.name = "Affiche_Niveau";
        affiche.event = "Cyclique";
        affiche.body = "IF Niveau_Cuve_1 > 50.0 THEN\n  IHM_POPUP('Pop_Niveau');\nEND_IF;\n";
        p.programs.scripts.push_back(affiche);
        const std::string q = "Montre-moi les scripts qui peuvent influencer Niveau_Cuve_1";
        const auto d = dg::answer(q, items, &p);
        check(has(d.text, "**D'apr\xC3\xA8s le projet ouvert**") && has(d.text, "Lissage") && has(d.text, "Affiche_Niveau"), "scripts : le graphe du projet (" + d.text + ")");
        const auto l1 = d.text.find("Lissage"), l2 = d.text.find("Affiche_Niveau");
        check(l1 != std::string::npos && d.text.find("**l'\xC3\xA9" "crit**", l1) < d.text.find('\n', l1), "scripts : Lissage l'\xC3\xA9" "crit");
        check(l2 != std::string::npos && d.text.find("la lit (sans la modifier)", l2) < d.text.find('\n', l2), "scripts : Affiche_Niveau la lit seulement");
        check(has(d.text, "`Automate_Cuves`") && hasRef(d, "script", "Lissage"), "scripts : l'\xC3\xA9quipement de la variable, les liens");
        const auto e = dg::answer(q, items, nullptr);
        check(has(e.text, "aucun incident") && has(e.text, "`Niveau_Cuve_1`"), "scripts : sans projet ni incident qui la cite, dit");
    }

    // ---- 12. la communication explique-t-elle les alarmes de mesure ? ----------------------------------------------
    {
        const auto a = ask("Est-ce que les d\xC3\xA9" "fauts de communication expliquent les alarmes de mesure ?");
        check(a.question == Q::Communication, "communication : la sorte");
        check(has(a.text, "`NIVEAU_CUVE_1_HAUT` (2026-10-31 11:00:05) \xE2\x80\x94 **peut s'expliquer** par la communication (relation indirecte)")
                  && has(a.text, "`Automate_Cuves` a perdu la communication 2,0 s avant"),
              "communication : peut expliquer (relation indirecte), l'\xC3\xA9" "cart");
        check(has(a.text, "`NIVEAU_CUVE_2_BAS` : potentiellement secondaire"), "communication : l'alarme potentiellement secondaire");
        check(has(a.text, "**Sans lien connu avec la communication**") && has(a.text, "`TEMPERATURE_FOUR`")
                  && has(a.text, "la condition ne lit aucune de ses variables"),
              "communication : une perte d'un autre \xC3\xA9quipement n'explique rien");
        check(has(a.text, "relation indirecte, pas une preuve"), "communication : pas une preuve");
        const auto nca = a.text.find("**Sans lien connu");
        check(nca != std::string::npos && a.text.find("POMPE_2_PRESSION_HAUTE", nca) != std::string::npos, "communication : les autres alarmes de mesure, sans lien");
    }

    // ---- 13. la liste ; le sujet dans des formes diverses ------------------------------------------------------------
    {
        const auto a = ask("Quels sont les incidents en attente ?");
        check(a.question == Q::List && has(a.text, "**8 incidents** : 7 en attente, 1 valid\xC3\xA9(s)"), "liste : le compte (" + a.text.substr(0, 80) + ")");
        check(!has(a.text, "2026-10-30 08:00:00"), "liste : en attente seulement");
        check(!has(a.text, "\xE2\x80\x94  ("), "liste : pas de synth\xC3\xA8se vide");
        const auto b = ask("pourquoi l alarme pompe_2_defaut_marche est apparue");
        check(has(b.text, "`POMPE_2_DEFAUT_MARCHE`") && has(b.text, "`Pompe_2.Defaut` = non disponible"), "sujet : sans casse ni ponctuation");
    }

    // ---- 13b. les suggestions, posees a leur tour : comprises, sur le bon incident ------------------------------------
    {
        const auto a = ask("Pourquoi l'alarme POMPE_2_PRESSION_HAUTE est-elle apparue ?");
        for (const auto& s : a.suggestions) {
            const auto b = ask(s);
            check(has(b.text, "POMPE_2_PRESSION_HAUTE"), "la suggestion \xC2\xAB " + s + " \xC2\xBB reste sur POMPE_2_PRESSION_HAUTE");
        }
    }

    // ---- 14. le contexte de l'IA distante : borne, cible ------------------------------------------------------------
    {
        const std::string c = dg::contextBlock("Compare les trois derniers incidents de la pompe 2", items);
        check(c.size() <= 6000, "contexte : au plus 6 Ko (" + std::to_string(c.size()) + ")");
        check(has(c, "POMPE_2_DEFAUT_MARCHE") && has(c, "POMPE_2_PRESSION_HAUTE") && !has(c, "ARRET_URGENCE") && !has(c, "NIVEAU_CUVE"),
              "contexte : les incidents que la demande concerne");
        check(has(c, "[hypoth\xC3\xA8se]") && has(c, "Station_Modbus (direct)") && has(c, "non disponible"), "contexte : pistes, \xC3\xA9quipements, valeurs");
        const std::string d = dg::contextBlock("Pourquoi cinq alarmes sont-elles apparues en m\xC3\xAAme temps ?", items, 6000, 3);
        check(has(d, "Les 3 derniers") && has(d, "DEFAUT_CONVOYEUR_1") && has(d, "chronologie"), "contexte : les trois derniers (sans sujet)");
        // un gros projet : 200 incidents a la chaine longue - toujours borne
        std::vector<Item> many;
        for (int i = 0; i < 200; ++i) {
            Item it = pressure(("x" + std::to_string(i)).c_str(), "2026-10-31 09:00:00.000", 20.0 + i, "8.9", State::Pending);
            for (int k = 0; k < 100; ++k) it.snapshot.chain.push_back(link("variable", "Variable_Longue_Numero_Quelque_Chose", "lue par la condition"));
            it.snapshot.summary = std::string(3000, 'x');
            many.push_back(std::move(it));
        }
        const std::string big = dg::contextBlock("Pourquoi cette alarme est-elle apparue ?", many);
        check(big.size() <= 6000, "contexte : born\xC3\xA9 m\xC3\xAAme sur 200 gros incidents (" + std::to_string(big.size()) + ")");
        const auto ans = dg::answer("Quel \xC3\xA9quipement est commun \xC3\xA0 ces diff\xC3\xA9rents d\xC3\xA9" "fauts ?", many);
        check(has(ans.text, "les 10 derniers incidents") || has(ans.text, "10 derniers incidents"), "\xC3\xA9quipement commun : born\xC3\xA9 aux 10 derniers");
    }

    std::printf("ai_incident_answers_test : %d r\xC3\xA9ussis, %d \xC3\xA9" "checs\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
