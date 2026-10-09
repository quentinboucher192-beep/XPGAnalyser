#include "HmiStandalone.hpp"
#include "HmiCheck.hpp"
#include "HmiTypeRegistry.hpp"

#include "../domain/ProjectModel.hpp"

#include <algorithm>
#include <cctype>
#include <map>
#include <set>

namespace hmi::standalone {
namespace {

std::string upper(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// La racine d'un chemin : "Armoires[0].ana" -> "Armoires".
std::string rootOf(std::string_view path) {
    const auto cut = path.find_first_of(".[ ");
    return std::string(path.substr(0, cut));
}

class Converter {
public:
    Converter(Project& p, const domain::Project& plc, Report& r) : p_(p), plc_(plc), r_(r) {
        for (const auto& t : p_.programs.types) hmiTypes_.insert(upper(t.name));
    }

    // Le type IHM d'un type de l'automate ; vide : pas de traduction (`why`).
    std::string typeOf(const domain::TypeRef& t, std::string& why, int depth = 0) {
        if (depth > 12) {
            why = "types imbriqu\xC3\xA9s trop profond\xC3\xA9ment";
            return {};
        }
        const std::string name = text(t.name);
        switch (t.klass) {
            case domain::TypeClass::Array: {
                if (t.arrayHigh < t.arrayLow) {
                    why = "tableau sans bornes (" + name + ")";
                    return {};
                }
                std::string element;
                const std::string elementName = text(t.elementType);
                if (const auto di = ddtNamed(elementName); di != domain::kNoIndex) element = ddt(di, why, depth + 1);
                else if (const auto fi = fbNamed(elementName); fi != domain::kNoIndex) element = fb(fi, why, depth + 1);
                else element = elementary(elementName, why);
                if (element.empty()) return {};
                return "ARRAY[" + std::to_string(t.arrayLow) + ".." + std::to_string(t.arrayHigh) + "] OF " + element;
            }
            case domain::TypeClass::Derived:
                if (t.derivedIndex != domain::kNoIndex && t.derivedIndex < plc_.derivedTypes.size()) return ddt(t.derivedIndex, why, depth + 1);
                if (const auto di = ddtNamed(name); di != domain::kNoIndex) return ddt(di, why, depth + 1);
                why = "le type " + name + " n'est pas dans le programme";
                return {};
            case domain::TypeClass::FunctionBlock:
                if (t.fbTypeIndex != domain::kNoIndex && t.fbTypeIndex < plc_.pous.size()) return fb(t.fbTypeIndex, why, depth + 1);
                if (const auto fi = fbNamed(name); fi != domain::kNoIndex) return fb(fi, why, depth + 1);
                why = "le bloc " + name + " n'est pas dans le programme";
                return {};
            default:
                if (const auto di = ddtNamed(name); di != domain::kNoIndex) return ddt(di, why, depth + 1);
                return elementary(name, why);
        }
    }

    std::string text(domain::SymbolId id) const { return std::string(plc_.strings.text(id)); }

private:
    // Un type elementaire de l'automate, dans le registre des types de l'IHM.
    std::string elementary(const std::string& name, std::string& why) const {
        std::string u = upper(name);
        if (const auto b = u.find('['); b != std::string::npos && u.rfind("STRING", 0) == 0) u = "STRING";   // STRING[32]
        if (u == "EBOOL") u = "BOOL";
        if (u == "TOD") u = "TIME_OF_DAY";
        if (u == "DT") u = "DATE_AND_TIME";
        if (u.empty()) {
            why = "type inconnu";
            return {};
        }
        if (!typereg::baseRegistry().byName(u)) {
            why = "le type " + name + " n'existe pas dans l'IHM";
            return {};
        }
        return u;
    }

    domain::Index ddtNamed(const std::string& name) const {
        const std::string u = upper(name);
        for (domain::Index i = 0; i < plc_.derivedTypes.size(); ++i)
            if (upper(text(plc_.derivedTypes[i].name)) == u) return i;
        return domain::kNoIndex;
    }
    domain::Index fbNamed(const std::string& name) const {
        const std::string u = upper(name);
        for (domain::Index i = 0; i < plc_.pous.size(); ++i)
            if (plc_.pous[i].kind == domain::PouKind::FunctionBlockType && upper(text(plc_.pous[i].name)) == u) return i;
        return domain::kNoIndex;
    }

    // Un type IHM de meme nom ; deja la (dans l'IHM, ou fait juste avant) : le meme.
    HmiType* typeNamed(const std::string& name) {
        for (auto& t : p_.programs.types)
            if (upper(t.name) == upper(name)) return &t;
        return nullptr;
    }

    std::string ddt(domain::Index i, std::string& why, int depth) {
        const auto& d = plc_.derivedTypes[i];
        const std::string name = text(d.name);
        if (doing_.count(upper(name))) return name;                 // une recursion : le nom suffit
        if (hmiTypes_.count(upper(name)) && !made_.count(upper(name))) return name;   // l'IHM l'avait deja
        if (made_.count(upper(name))) return name;
        doing_.insert(upper(name));
        HmiType t;
        t.id = p_.allocate();
        t.name = name;
        t.description = "Le DDT " + name + " de l'automate (1.12.0 : l'IHM autonome)";
        t.folder = "Automate";
        for (const auto fi : d.fields) {
            if (fi >= plc_.variables.size()) continue;
            const auto& f = plc_.variables[fi];
            std::string w;
            const std::string ft = typeOf(f.type, w, depth + 1);
            if (ft.empty()) {
                r_.skipped.push_back(name + "." + text(f.name) + " : " + w);
                continue;
            }
            TypeMember m;
            m.name = text(f.name);
            m.type = ft;
            m.initial = text(f.initValue);
            m.description = text(f.comment);
            t.members.push_back(std::move(m));
        }
        doing_.erase(upper(name));
        if (t.members.empty()) {
            why = "le DDT " + name + " n'a aucun membre traduisible";
            return {};
        }
        made_.insert(upper(name));
        hmiTypes_.insert(upper(name));
        p_.programs.types.push_back(std::move(t));
        r_.types.push_back(name);
        return name;
    }

    std::string fb(domain::Index i, std::string& why, int depth) {
        const auto& pou = plc_.pous[i];
        const std::string name = text(pou.name);
        if (doing_.count(upper(name)) || made_.count(upper(name)) || (hmiTypes_.count(upper(name)))) return name;
        doing_.insert(upper(name));
        HmiType t;
        t.id = p_.allocate();
        t.name = name;
        t.description = "Le bloc " + name + " de l'automate : ses entr\xC3\xA9" "es et ses sorties (1.12.0 : l'IHM autonome)";
        t.folder = "Automate";
        for (const auto vi : pou.parameters) {
            if (vi >= plc_.variables.size()) continue;
            const auto& v = plc_.variables[vi];
            std::string w;
            const std::string vt = typeOf(v.type, w, depth + 1);
            if (vt.empty()) continue;
            TypeMember m;
            m.name = text(v.name);
            m.type = vt;
            m.initial = text(v.initValue);
            m.description = text(v.comment);
            t.members.push_back(std::move(m));
        }
        doing_.erase(upper(name));
        if (t.members.empty()) {
            why = "le bloc " + name + " n'a ni entr\xC3\xA9" "e ni sortie connue (un bloc de la biblioth\xC3\xA8que)";
            return {};
        }
        made_.insert(upper(name));
        hmiTypes_.insert(upper(name));
        p_.programs.types.push_back(std::move(t));
        r_.types.push_back(name);
        return name;
    }

    Project& p_;
    const domain::Project& plc_;
    Report& r_;
    std::set<std::string> hmiTypes_, made_, doing_;
};

} // namespace

Report fromPlc(Project& p, const domain::Project& plc) {
    Report r;
    // Les globales de l'automate, par nom.
    std::map<std::string, domain::Index> globals;
    for (domain::Index i = 0; i < plc.variables.size(); ++i) {
        const auto& v = plc.variables[i];
        if (v.scope != domain::VariableScope::Global && v.scope != domain::VariableScope::Constant) continue;
        const std::string n(plc.strings.text(v.name));
        if (!n.empty()) globals.emplace(upper(n), i);
    }
    if (globals.empty()) return r;

    // Les noms que l'IHM ne connait pas elle-meme : Compiler les demande a « l'automate » (ici,
    // la sonde les note et dit oui) - les vues, les actions, les scripts, les fonctions, les
    // alarmes, les recettes, les historiques ; puis la table des adresses.
    std::set<std::string> asked;
    const auto probe = [&asked](std::string_view root) {
        asked.insert(upper(root));
        return true;
    };
    (void)compileWith(p, probe);
    // ... et Generer : ce que lit la propriete « variable » d'un objet (un afficheur, un
    // voyant de la vue Communication...) n'est controle que par lui.
    (void)generateWith(p, probe, GenerateOptions{});
    for (const auto& a : p.comm.addresses) asked.insert(upper(rootOf(a.variable)));

    Converter conv(p, plc, r);
    std::vector<std::pair<std::string, domain::Index>> wanted;
    for (const auto& name : asked) {
        const auto it = globals.find(name);
        if (it == globals.end()) continue;
        if (p.variable(std::string(plc.strings.text(plc.variables[it->second].name)))) continue;   // deja une variable IHM
        wanted.emplace_back(name, it->second);
    }

    // L'automate relie par Modbus TCP : il devient un equipement, ses variables situees y sont liees.
    const bool modbus = p.comm.modbus();
    std::string equipment;
    if (modbus && !wanted.empty()) {
        equipment = kPlcEquipment;
        for (int k = 2; std::any_of(p.equipments.begin(), p.equipments.end(), [&](const Equipment& e) { return upper(e.name) == upper(equipment); }); ++k)
            equipment = std::string(kPlcEquipment) + "_" + std::to_string(k);
        Equipment e;
        e.id = p.allocate();
        e.name = equipment;
        e.type = EquipmentType::ModbusTcp;
        e.host = p.comm.host;
        e.port = p.comm.port;
        e.unit = p.comm.unit;
        e.timeoutMs = p.comm.timeoutMs;
        e.periodMs = p.comm.periodMs;
        e.retryS = p.comm.retryS;
        e.wordOrder = p.comm.wordOrder;
        e.maxWords = p.comm.maxWords;
        e.maxBits = p.comm.maxBits;
        e.gap = p.comm.gap;
        e.writes = p.comm.writes;
        e.badAfterS = p.comm.badAfterS;
        e.description = "L'automate du projet (1.12.0 : l'IHM autonome le lit en \xC3\xA9quipement Modbus TCP)";
        p.equipments.push_back(std::move(e));
        r.equipment = equipment;
    }
    // Les adresses de la table (une variable non situee, ou un membre), par racine.
    std::map<std::string, std::string> tableAddress;
    for (const auto& a : p.comm.addresses)
        if (upper(rootOf(a.variable)) == upper(a.variable)) tableAddress.emplace(upper(a.variable), a.address);

    for (const auto& [key, index] : wanted) {
        const auto& v = plc.variables[index];
        const std::string name(plc.strings.text(v.name));
        std::string why;
        const std::string type = conv.typeOf(v.type, why);
        if (type.empty()) {
            r.skipped.push_back(name + " : " + why);
            continue;
        }
        Variable hv;
        hv.id = p.allocate();
        hv.name = name;
        hv.type = type;
        hv.initial = std::string(plc.strings.text(v.initValue));
        if (hv.initial.empty()) hv.initial = type == "BOOL" ? "FALSE" : type == "STRING" ? "''" : type.rfind("ARRAY", 0) == 0 || !typereg::baseRegistry().byName(type) ? "" : "0";
        hv.description = std::string(plc.strings.text(v.comment));
        hv.folder = "Automate";
        if (!equipment.empty()) {
            std::string address = v.located && v.address.valid() ? v.address.raw : std::string{};
            if (address.empty())
                if (const auto it = tableAddress.find(key); it != tableAddress.end()) address = it->second;
            if (!address.empty() && address[0] == '%' && (address.size() < 2 || (address[1] != 'I' && address[1] != 'Q' && address[1] != 'S' && address[1] != 'K'))) {
                hv.equipment = equipment;
                hv.address = address;
                r.bound.push_back(name);
            }
        }
        p.programs.variables.push_back(std::move(hv));
        r.variables.push_back(name);
    }
    if (!r.variables.empty() && std::find(p.programs.folders.begin(), p.programs.folders.end(), "Automate") == p.programs.folders.end())
        p.programs.folders.push_back("Automate");
    return r;
}

std::string describe(const Report& r) {
    std::string out;
    const auto line = [&out](const std::string& s) { out += s + "\n"; };
    if (!r.variables.empty()) line(std::to_string(r.variables.size()) + " variable(s) IHM cr\xC3\xA9\xC3\xA9" "e(s) depuis l'automate (dossier Automate)");
    if (!r.types.empty()) line(std::to_string(r.types.size()) + " type(s) IHM : " + [&] {
        std::string t;
        for (const auto& n : r.types) t += (t.empty() ? "" : ", ") + n;
        return t;
    }());
    if (!r.equipment.empty()) line("l'\xC3\xA9quipement " + r.equipment + " (Modbus TCP) : " + std::to_string(r.bound.size()) + " variable(s) li\xC3\xA9" "e(s) \xC3\xA0 leur adresse");
    for (const auto& s : r.skipped) line("laiss\xC3\xA9" "e : " + s);
    return out;
}

} // namespace hmi::standalone
