// =============================================================================
//  app/hmi/HmiEquipmentLinkPane.cpp - 1.12.11 (lot G) : la section « Liaison
//                                     Equipement » de l'inspecteur. Voir
//                                     l'en-tete.
// =============================================================================
#include "HmiEquipmentLinkPane.hpp"

#include "../../hmi/HmiEquipmentLink.hpp"
#include "../../hmi/HmiMarkers.hpp"

namespace app::eqlinkpane {

using PG = ui::PropertyGrid;

bool choose(const hmi::DocumentPtr& doc, const Apply& apply, const std::string& type, const std::string& role, const std::string& choice) {
    if (!doc || !apply || type.empty()) return false;
    std::optional<std::string> member;
    if (choice != kByName) member = choice == kNoMember ? std::string{} : choice;
    auto cmd = hmi::changeProject(doc, "Liaison \xC3\x89quipement : " + type, [&](hmi::Project& p) { (void)hmi::eqlink::choose(p, type, role, member); });
    if (!cmd) return false;
    apply(std::move(cmd));
    return true;
}

std::vector<PG::Category> categories(const hmi::DocumentPtr& doc, const Apply& apply, hmi::Id viewId, hmi::Id objectId) {
    std::vector<PG::Category> out;
    const auto* v = doc ? doc->project.view(viewId) : nullptr;
    const auto* o = v ? v->object(objectId) : nullptr;
    if (!o || !hmi::kindIsSynoptic(o->kind) || hmi::eqlink::equipmentPath(*o).empty()) return out;
    const auto b = hmi::eqlink::bindingOf(doc->project, v, *o);
    PG::Category c;
    c.name = "Liaison \xC3\x89quipement" + (b.type.empty() ? std::string{} : " (" + b.type + ")");
    PG::Property head;
    head.name = "Structure";
    head.type = PG::ValueType::ReadOnly;
    head.value = b.type.empty() ? b.path + " : type inconnu" : b.path + " : " + b.type + " (" + std::to_string(b.members.size()) + " membres)";
    head.description = b.type.empty()
                           ? std::string("Le chemin ne d\xC3\xA9signe pas une structure connue (une variable IHM d'un type structure, un "
                                         "param\xC3\xA8tre typ\xC3\xA9 du symbole ou de la popup) : rien ne se lie. G\xC3\xA9n\xC3\xA9rer le dit.")
                           : "Ses membres se lient par leur nom ; une ligne choisie ici vaut pour toutes les " + b.type
                                 + " (Ctrl+Z la reprend). Ce qui est r\xC3\xA9gl\xC3\xA9 \xC3\xA0 la main sur l'objet gagne.";
    c.properties.push_back(std::move(head));
    if (b.type.empty()) {
        out.push_back(std::move(c));
        return out;
    }
    for (const auto& row : b.rows) {
        PG::Property p;
        p.name = std::string(row.role->label) + (row.byHand ? "  (\xC3\xA0 la main)" : "");
        p.type = PG::ValueType::Enum;
        p.enumValues = {kByName, kNoMember};
        for (const auto& m : b.members) p.enumValues.push_back(m.name);
        p.value = row.member.empty() ? std::string(kNoMember) : row.member;
        std::string names;
        for (const auto n : row.role->names)
            if (!n.empty()) names += (names.empty() ? "" : ", ") + std::string(n);
        std::string help = row.byHand ? "R\xC3\xA9gl\xC3\xA9 \xC3\xA0 la main sur l'objet : il gagne, la liaison ne le remplit pas."
                         : row.member.empty() ? "Aucun membre lu."
                                              : "Lu : " + row.source + ".";
        help += row.from == hmi::eqlink::From::Chosen ? "\nChoisi pour toutes les " + b.type + " (" + std::string(kByName) + " : revenir aux noms)."
                                                      : "\nPar son nom (cherch\xC3\xA9s dans l'ordre : " + names + ").";
        p.description = std::move(help);
        const std::string type = b.type, role(row.role->key);
        p.commit = [doc, apply, type, role](std::string_view s) { return choose(doc, apply, type, role, std::string(s)); };
        c.properties.push_back(std::move(p));
    }
    out.push_back(std::move(c));
    return out;
}

} // namespace app::eqlinkpane
