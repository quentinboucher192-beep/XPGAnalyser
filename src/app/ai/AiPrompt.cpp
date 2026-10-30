// =============================================================================
//  app/ai/AiPrompt.cpp - la premiere forme du contrat (le chantier du registre la
//  remplace : le registre des genres tire du code, le contexte, les connaissances)
// =============================================================================
#include "AiPrompt.hpp"

#include "AiProvider.hpp"

namespace app::ai {

std::string systemPrompt(const PromptInput& in) {
    std::string s =
        "Tu es l'Assistant IA d'IHM Studio, un logiciel de conception d'IHM industrielles. "
        "Tu reponds en francais. Pour modifier la vue active, tu appelles l'outil ";
    s += kOperationsTool;
    s += " ; tu n'inventes jamais une variable, une propriete ni un genre d'objet.";
    if (in.view) s += "\nVue active : " + in.view->name;
    return s;
}

std::string operationsTool() {
    json::Value tool = json::Value::object();
    tool.set("name", std::string(kOperationsTool));
    tool.set("description", "Propose des operations sur la vue active d'IHM Studio.");
    json::Value schema = json::Value::object();
    schema.set("type", "object");
    json::Value props = json::Value::object();
    json::Value resume = json::Value::object();
    resume.set("type", "string");
    props.set("resume", resume);
    json::Value ops = json::Value::object();
    ops.set("type", "array");
    json::Value item = json::Value::object();
    item.set("type", "object");
    ops.set("items", item);
    props.set("operations", ops);
    schema.set("properties", props);
    json::Value required = json::Value::array();
    required.push("operations");
    schema.set("required", required);
    tool.set("input_schema", schema);
    json::Value tools = json::Value::array();
    tools.push(tool);
    return json::write(tools, false);
}

std::optional<Proposal> proposalFromTool(std::string_view inputJson, const PromptInput& in, std::string_view request,
                                         std::string* why) {
    json::Value v;
    std::string err;
    if (!json::parse(inputJson, v, &err) || !v.isObject()) {
        if (why) *why = "L'entr\xC3\xA9" "e de l'outil est illisible (" + err + ").";
        return std::nullopt;
    }
    Proposal p;
    if (!operationsFromJson(v.at("operations"), p.ops, why)) return std::nullopt;
    p.id = newId("op");
    p.request = std::string(request);
    p.summary = v.str("resume");
    if (in.view) {
        p.view = in.view->name;
        p.viewId = in.view->id;
        p.fingerprint = fingerprint(*in.view);
    }
    return p;
}

} // namespace app::ai
