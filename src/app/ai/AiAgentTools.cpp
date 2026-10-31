// =============================================================================
//  app/ai/AiAgentTools.cpp - 1.21.0 : les outils de l'agent (voir l'en-tete)
// =============================================================================
#include "AiAgentTools.hpp"

#include "AiJson.hpp"
#include "AiProjectAnswers.hpp"
#include "AiSandbox.hpp"

#include "../../hmi/HmiDiscovery.hpp"
#include "../../hmi/HmiModel.hpp"

#include <cctype>
#include <utility>

namespace app::ai::agent {

namespace json = app::ai::json;
namespace aw = app::ai::answers;

namespace {

std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

// La question de l'IA (un mot cle stable) -> la question au graphe.
bool questionOf(std::string_view key, aw::Question& q) {
    const std::string k = lower(key);
    if (k == "utilisateurs" || k == "qui_utilise") q = aw::Question::Users;
    else if (k == "objets") q = aw::Question::ObjectsUsing;
    else if (k == "scripts") q = aw::Question::ScriptsUsing;
    else if (k == "alarmes") q = aw::Question::AlarmsOf;
    else if (k == "impact" || k == "suppression") q = aw::Question::Impact;
    else if (k == "equipements_vue") q = aw::Question::ViewEquipments;
    else if (k == "orphelins" || k == "inutilises") q = aw::Question::Orphans;
    else return false;
    return true;
}

std::string definitionJson() {
    json::Value t = json::Value::object();
    t.set("name", std::string(kAnalyseTool));
    t.set("description",
          "Interroge le projet ouvert en LECTURE SEULE (rien n'est modifi\xC3\xA9) : le graphe r\xC3\xA9" "el des relations. Questions : "
          "utilisateurs (qui utilise le sujet), objets, scripts, alarmes (ce qui concerne le sujet), impact (ce qu'une suppression "
          "du sujet toucherait : directement, indirectement, ce qui deviendrait orphelin), equipements_vue (les \xC3\xA9quipements "
          "d'une vue), orphelins (les \xC3\xA9l\xC3\xA9ments inutilis\xC3\xA9s), verifier (la v\xC3\xA9rification globale : erreurs, "
          "avertissements), conventions (les pr\xC3\xA9" "fixes, s\xC3\xA9ries et plages d'adresses du projet). Le sujet : un nom r\xC3\xA9" "el du "
          "projet (variable, type, vue, objet, alarme, script, fonction, \xC3\xA9quipement). Avant de proposer une suppression, "
          "demande son impact. La r\xC3\xA9ponse cite ce que le graphe a trouv\xC3\xA9, jamais une d\xC3\xA9pendance invent\xC3\xA9" "e.");
    json::Value props = json::Value::object();
    json::Value q = json::Value::object();
    q.set("type", "string");
    json::Value en = json::Value::array();
    for (const char* k : {"utilisateurs", "objets", "scripts", "alarmes", "impact", "equipements_vue", "orphelins", "verifier", "conventions"}) en.push(k);
    q.set("enum", std::move(en));
    props.set("question", std::move(q));
    json::Value s = json::Value::object();
    s.set("type", "string");
    s.set("description", "Le nom de l'\xC3\xA9l\xC3\xA9ment (vide pour orphelins, verifier, conventions).");
    props.set("sujet", std::move(s));
    json::Value schema = json::Value::object();
    schema.set("type", "object");
    schema.set("properties", std::move(props));
    json::Value req = json::Value::array();
    req.push("question");
    schema.set("required", std::move(req));
    t.set("input_schema", std::move(schema));
    return json::write(t, false);
}

} // namespace

std::string analyse(const hmi::Project& project, std::string_view inputJson) {
    json::Value in;
    std::string err;
    if (!json::parse(inputJson, in, &err) || !in.isObject()) return "Entr\xC3\xA9" "e illisible : " + err + ". Rien n'a \xC3\xA9t\xC3\xA9 analys\xC3\xA9.";
    const std::string question = lower(in.str("question"));
    const std::string subject = in.str("sujet");
    aw::Answer a;
    if (question == "verifier") {
        a = aw::verify(project);
        if (a.proposal) a.text += "\n\n(Des corrections sont possibles : elles ne sont PAS appliqu\xC3\xA9" "es. Pour les faire, propose-les avec "
                                  "proposer_operations - l'utilisateur les valide.)";
    } else if (question == "conventions") {
        a = aw::conventions(project);
    } else {
        aw::Question q{};
        if (!questionOf(question, q))
            return "Question inconnue : " + question + " (utilisateurs, objets, scripts, alarmes, impact, equipements_vue, orphelins, "
                   "verifier, conventions).";
        if (q != aw::Question::Orphans && subject.empty()) return "Il manque le sujet (le nom d'un \xC3\xA9l\xC3\xA9ment du projet).";
        a = aw::relations(project, q, subject);
    }
    std::string out = "Analyse du projet (lecture seule, le graphe r\xC3\xA9" "el) - " + question + (subject.empty() ? std::string{} : " : " + subject) + "\n";
    out += a.text.empty() ? std::string("(rien trouv\xC3\xA9)") : a.text;
    if (!a.understood) out += "\n(Le sujet n'a pas \xC3\xA9t\xC3\xA9 trouv\xC3\xA9 dans le projet : ne l'invente pas.)";
    return out;
}

LocalTool analyseTool(std::shared_ptr<const hmi::Project> snapshot) {
    LocalTool t;
    t.name = std::string(kAnalyseTool);
    t.definitionJson = definitionJson();
    t.maxCalls = kMaxAnalyses;
    t.run = [snap = std::move(snapshot)](const std::string& input, const std::function<void(const std::string&)>& status,
                                         const std::atomic<bool>&) -> std::string {
        json::Value v;
        (void)json::parse(input, v);
        status("Analyse du projet : " + v.str("question") + (v.str("sujet").empty() ? std::string{} : " " + v.str("sujet")) + "\xE2\x80\xA6");
        return snap ? analyse(*snap, input) : std::string("Pas de projet ouvert : rien \xC3\xA0 analyser.");
    };
    return t;
}

LocalTool sandboxTool(std::shared_ptr<const hmi::Project> snapshot, std::string program) {
    LocalTool t;
    t.name = std::string(sandbox::kTool);
    t.definitionJson = sandbox::toolJson();
    t.maxCalls = sandbox::kMaxRuns;
    t.run = [snap = std::move(snapshot), program = std::move(program)](const std::string& input, const std::function<void(const std::string&)>& status,
                                                                       const std::atomic<bool>& cancelled) -> std::string {
        sandbox::Job job;
        std::string bad;
        if (!sandbox::jobFromJson(input, job, &bad))
            return "Entr\xC3\xA9" "e refus\xC3\xA9" "e : " + bad + ". Rien n'a \xC3\xA9t\xC3\xA9 ex\xC3\xA9" "cut\xC3\xA9.";
        status("Bac \xC3\xA0 sable : " + job.name + " (" + job.language + ")\xE2\x80\xA6");
        static const hmi::Project kEmpty;
        const sandbox::Outcome o = sandbox::run(job, snap ? *snap : kEmpty, &cancelled, program);
        const sandbox::Stage* comp = o.stage("compilation");
        std::string said = "Bac \xC3\xA0 sable : " + job.name + " - ";
        if (!o.error.empty()) said += o.error;
        else if (o.testsPassed()) said += std::to_string(o.cases.size()) + " cas r\xC3\xA9ussis";
        else if (comp && comp->state == sandbox::State::Ok && o.cases.empty()) said += "compil\xC3\xA9" "e, aucun cas";
        else said += "\xC3\xA0 corriger";
        status(said);
        return o.report();
    };
    return t;
}

std::string askScan(Bridge& bridge, std::string_view inputJson) {
    json::Value in;
    std::string err;
    if (!json::parse(inputJson, in, &err) || !in.isObject())
        return "Entr\xC3\xA9" "e illisible : " + err + ". Rien n'a \xC3\xA9t\xC3\xA9 pr\xC3\xA9par\xC3\xA9, rien n'est parti.";
    {
        std::lock_guard<std::mutex> lk(bridge.m);
        if (!bridge.scanAvailable)
            return "La d\xC3\xA9tection r\xC3\xA9seau n'est pas disponible ici (aucun projet IHM ouvert). Rien n'est parti.";
    }
    std::vector<std::string> protocols;
    for (const auto& p : in.at("protocoles").items())
        if (p.isString()) protocols.push_back(p.asString());
    hmi::discovery::Request rq;
    std::string why;
    // Les interfaces du PC ne se lisent que sur le fil de l'interface : la fenetre les propose (et le resume le dira).
    if (!hmi::discovery::requestFrom(in.str("plage"), protocols, in.num("cadence", 0.0), rq, {}, &why))
        return "Demande refus\xC3\xA9" "e : " + why + ". Rien n'est parti.";
    const hmi::discovery::Plan pl = hmi::discovery::plan(rq, {});
    if (!pl.ok()) return "Demande refus\xC3\xA9" "e : " + pl.error + ". Rien n'est parti.";
    const std::string reason = in.str("raison");
    {
        std::lock_guard<std::mutex> lk(bridge.m);
        if (bridge.scanAsks.size() < 4) bridge.scanAsks.push_back({hmi::discovery::encodeRequest(rq), reason});
    }
    return "La fen\xC3\xAAtre de confirmation de la d\xC3\xA9tection s'ouvre chez l'utilisateur, pr\xC3\xA9remplie :\n" + hmi::discovery::summaryText(pl)
           + "\nRIEN N'EST PARTI : c'est l'utilisateur qui lance (ou annule) ; il peut changer l'interface, la plage, les "
             "protocoles. La d\xC3\xA9tection n'est qu'une observation (aucune \xC3\xA9" "criture, aucune commande). Quand elle sera "
             "finie, resultat_detection rend le compte rendu r\xC3\xA9" "el ; n'en invente aucun.";
}

LocalTool scanTool(std::shared_ptr<Bridge> bridge) {
    LocalTool t;
    t.name = std::string(kScanTool);
    json::Value d = json::Value::object();
    d.set("name", std::string(kScanTool));
    d.set("description",
          "Pr\xC3\xA9pare une d\xC3\xA9tection des \xC3\xA9quipements du r\xC3\xA9seau (lecture seule : ping, connexion TCP referm\xC3\xA9" "e, "
          "identification Modbus) et OUVRE LA FEN\xC3\x8ATRE DE CONFIRMATION chez l'utilisateur, pr\xC3\xA9remplie. Tu ne lances jamais "
          "un scan : l'utilisateur v\xC3\xA9rifie la plage et clique Lancer. Ne propose que la plage qu'il a donn\xC3\xA9" "e ou celle "
          "du projet (jamais un r\xC3\xA9seau entier sans raison). Ensuite, resultat_detection rend le compte rendu.");
    json::Value props = json::Value::object();
    json::Value range = json::Value::object();
    range.set("type", "string");
    range.set("description", "192.168.1.0/24, 192.168.1.10-40 ou une adresse (4096 adresses au plus).");
    props.set("plage", std::move(range));
    json::Value prot = json::Value::object();
    prot.set("type", "array");
    json::Value item = json::Value::object();
    item.set("type", "string");
    prot.set("items", std::move(item));
    prot.set("description", "Modbus TCP, S7, EtherNet/IP, OPC UA, web... (vide : ceux par d\xC3\xA9" "faut).");
    props.set("protocoles", std::move(prot));
    json::Value rate = json::Value::object();
    rate.set("type", "number");
    rate.set("description", "Interrogations par seconde (0 : 20 par d\xC3\xA9" "faut ; 100 au plus).");
    props.set("cadence", std::move(rate));
    json::Value why = json::Value::object();
    why.set("type", "string");
    why.set("description", "Pourquoi (montr\xC3\xA9 \xC3\xA0 l'utilisateur dans la confirmation).");
    props.set("raison", std::move(why));
    json::Value schema = json::Value::object();
    schema.set("type", "object");
    schema.set("properties", std::move(props));
    json::Value req = json::Value::array();
    req.push("plage");
    schema.set("required", std::move(req));
    d.set("input_schema", std::move(schema));
    t.definitionJson = json::write(d, false);
    t.maxCalls = 2;
    t.run = [b = std::move(bridge)](const std::string& input, const std::function<void(const std::string&)>& status,
                                    const std::atomic<bool>&) -> std::string {
        status("D\xC3\xA9tection r\xC3\xA9seau : la confirmation s'ouvre\xE2\x80\xA6");
        return b ? askScan(*b, input) : std::string("La d\xC3\xA9tection r\xC3\xA9seau n'est pas disponible ici.");
    };
    return t;
}

LocalTool scanResultTool(std::shared_ptr<Bridge> bridge) {
    LocalTool t;
    t.name = std::string(kScanResultTool);
    json::Value d = json::Value::object();
    d.set("name", std::string(kScanResultTool));
    d.set("description", "Le compte rendu r\xC3\xA9" "el du dernier scan r\xC3\xA9seau termin\xC3\xA9 (appareils et ce qui est confirm\xC3\xA9, "
                         "annonc\xC3\xA9 ou suppos\xC3\xA9 ; comparaison avec le projet ; propositions). Lecture seule.");
    json::Value schema = json::Value::object();
    schema.set("type", "object");
    schema.set("properties", json::Value::object());
    d.set("input_schema", std::move(schema));
    t.definitionJson = json::write(d, false);
    t.maxCalls = 3;
    t.run = [b = std::move(bridge)](const std::string&, const std::function<void(const std::string&)>&, const std::atomic<bool>&) -> std::string {
        if (!b) return "Aucun scan.";
        std::lock_guard<std::mutex> lk(b->m);
        if (b->scanSerial == 0 || b->scanText.empty())
            return "Aucun scan termin\xC3\xA9 dans cette s\xC3\xA9" "ance : l'utilisateur n'a pas (encore) lanc\xC3\xA9 la d\xC3\xA9tection. N'invente "
                   "aucun appareil.";
        return "Compte rendu du dernier scan (r\xC3\xA9" "el) :\n" + b->scanText;
    };
    return t;
}

} // namespace app::ai::agent
