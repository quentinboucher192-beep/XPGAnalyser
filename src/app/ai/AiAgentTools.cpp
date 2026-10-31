// =============================================================================
//  app/ai/AiAgentTools.cpp - 1.21.0 : les outils de l'agent (voir l'en-tete)
// =============================================================================
#include "AiAgentTools.hpp"

#include "AiJson.hpp"
#include "AiProjectAnswers.hpp"
#include "AiSandbox.hpp"

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

} // namespace app::ai::agent
