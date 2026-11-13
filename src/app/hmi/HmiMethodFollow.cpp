// =============================================================================
//  app/hmi/HmiMethodFollow.cpp - 1.50.1 : la signature d'une virtuelle et ses redefinitions (voir l'en-tete)
// =============================================================================
#include "HmiMethodFollow.hpp"

#include "HmiPanels.hpp"                 // several::confirm : la question (l'hote ouvre la fenetre)
#include "../../hmi/HmiMethods.hpp"

#include <algorithm>
#include <memory>

namespace app {

std::string followQuestion(const hmi::Project& trial, hmi::Id method, const std::vector<std::string>& touched) {
    const hmi::HmiType* owner = nullptr;
    const hmi::HmiFunction* m = hmi::methodById(trial, method, &owner);
    if (!m || !owner) return {};
    std::string text = owner->name + "." + m->name + " est virtuelle : ";
    text += touched.size() == 1 ? std::string("sa red\xC3\xA9" "finition prend") : "ses " + std::to_string(touched.size()) + " red\xC3\xA9" "finitions prennent";
    text += " la m\xC3\xAAme signature, " + hmi::methodSignature(owner->name, *m).substr(owner->name.size() + 1) + " :";
    for (const auto& t : touched) text += "\n    \xE2\x80\xA2 " + t;
    text += "\nLeurs param\xC3\xA8tres suivent (un nom chang\xC3\xA9 l'est aussi dans leur corps, SUPER." + m->name
          + "(\xE2\x80\xA6) compris) ; leurs corps se lisent encore. Un seul Ctrl+Z reprend tout.";
    return text;
}

Follow followSignature(const hmi::DocumentPtr& doc, const std::function<void(core::CommandPtr)>& apply, hmi::Id method,
                       const hmi::Project& trial, const std::string& label, std::string* why, std::function<void()> after) {
    if (!doc) return Follow::None;
    const hmi::HmiType* owner = nullptr;
    const hmi::HmiFunction* before = hmi::methodById(doc->project, method, &owner);
    const hmi::HmiFunction* now = hmi::methodById(trial, method);
    if (!before || !owner || !now || !before->isVirtual || owner->kind != hmi::HmiTypeKind::Structure) return Follow::None;
    if (hmi::sameMethodHead(*before, *now)) return Follow::None;
    const auto overrides = hmi::overridesOf(doc->project, method);
    if (overrides.empty()) return Follow::None;
    // Les redefinitions suivent, sur la copie : un refus n'a rien touche.
    hmi::Project next = trial;
    std::vector<std::string> touched;
    std::string w;
    if (!hmi::followOverrides(next, method, *before, &touched, &w)) {
        if (why) *why = w;
        return Follow::Refused;
    }
    // Ce que la commande posera : le type de la methode et ceux des redefinitions, tels que la copie les a ; les
    // identifiants pris (un parametre ajoute).
    std::vector<hmi::HmiType> types;
    std::vector<hmi::Id> ids{owner->id};
    for (const auto& om : overrides)
        if (std::find(ids.begin(), ids.end(), om.owner->id) == ids.end()) ids.push_back(om.owner->id);
    for (const hmi::Id id : ids)
        if (const auto* t = next.hmiType(id)) types.push_back(*t);
    const hmi::Id nextId = next.nextId;
    const std::string name = owner->name + "." + before->name;
    const std::string title = "Changer la signature de " + name + " ?";
    const std::string text = followQuestion(next, method, touched);
    const std::string command = label + (touched.size() == 1 ? std::string(" (et sa red\xC3\xA9" "finition)")
                                                             : " (et ses " + std::to_string(touched.size()) + " red\xC3\xA9" "finitions)");
    several::confirm(title, text, touched.size() == 1 ? "Changer les deux" : "Changer partout",
                     [doc, apply, types = std::move(types), nextId, command, after = std::move(after)] {
                         auto cmd = hmi::changeProject(doc, command, [&](hmi::Project& p) {
                             for (const auto& t : types)
                                 if (auto* x = p.hmiType(t.id)) *x = t;
                             p.nextId = std::max(p.nextId, nextId);
                         });
                         if (cmd && apply) apply(std::move(cmd));
                         if (after) after();
                     });
    return Follow::Done;
}

} // namespace app
