// =============================================================================
//  app/hmi/HmiPartage.cpp - 1.12.27 : copier dans une fenetre, coller dans l'autre
//  (voir HmiPartage.hpp)
// =============================================================================
#include "HmiPartage.hpp"

#include "../Fenetres.hpp"
#include "../../hmi/HmiEdit.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <system_error>

namespace app::partage {

namespace fs = std::filesystem;
using hmi::Id;
using hmi::kNoId;

namespace {

// La vue de passage du paquet : un nom qu'aucun projet ne porte.
constexpr const char* kVue = "__Copie_entre_fenetres__";
constexpr const char* kMot = "copie-fenetre";

std::string normalise(std::string_view t) {
    std::string s;
    s.reserve(t.size());
    for (char c : t)
        if (c != '\r') s += c;
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ' || s.back() == '\t')) s.pop_back();
    return s;
}

const hmi::View* vueDe(const Copie& c) { return c.paquet.content.viewByName(kVue); }

// Un nom d'objet libre dans la vue ?
bool libre(const hmi::View& v, const std::string& name) {
    return !name.empty() && std::none_of(v.objects.begin(), v.objects.end(), [&](const hmi::Object& o) { return o.name == name; });
}

} // namespace

std::string empreinte(std::string_view texteSysteme) {
    const std::string s = normalise(texteSysteme);
    std::uint64_t h = 1469598103934665603ull;
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ull;
    }
    char buf[24];
    std::snprintf(buf, sizeof buf, "%016llx", static_cast<unsigned long long>(h));
    return buf;
}

std::string chemin() { return (fenetres::dossier() / "presse-papiers.xpgvues").string(); }

bool publier(const hmi::Project& p, Id from, const std::vector<hmi::Object>& objects, const std::string& texteSysteme) {
    if (objects.empty() || normalise(texteSysteme).empty()) return false;
    // Une vue de passage, dans une copie du projet : ce qu'elle emporte se cherche
    // comme pour une vue exportee (les symboles, les popups, les images, les styles,
    // les variables IHM...).
    hmi::Project tmp = p;
    hmi::View clip;
    clip.id = tmp.allocate();
    clip.name = kVue;
    clip.showHeader = false;
    clip.showFooter = false;
    std::string vueOrigine;
    if (const auto* src = p.view(from)) {
        clip.width = src->width;
        clip.height = src->height;
        clip.layers = src->layers;
        clip.activeLayer = src->activeLayer;
        vueOrigine = src->name;
    }
    clip.objects = objects;
    const Id clipId = clip.id;
    tmp.views.push_back(std::move(clip));
    hmi::pkg::Package pk = hmi::pkg::collect(tmp, {clipId});
    pk.manifest.name = "Presse-papiers";
    pk.manifest.description = std::string(kMot) + " " + std::to_string(fenetres::processusCourant()) + " " + empreinte(texteSysteme)
                            + " " + vueOrigine;
    std::error_code ec;
    const fs::path dir = fenetres::dossier();
    fs::create_directories(dir, ec);
    // Ecrit a cote puis renomme : une fenetre qui lit au meme moment voit l'ancien
    // paquet ou le nouveau, jamais la moitie d'un.
    const fs::path part = dir / ("presse-papiers." + std::to_string(fenetres::processusCourant()) + ".part");
    if (!hmi::pkg::writeFile(pk, part.string())) {
        fs::remove(part, ec);
        return false;
    }
    fs::rename(part, chemin(), ec);
    if (ec) {
        fs::remove(chemin(), ec);
        ec.clear();
        fs::rename(part, chemin(), ec);
    }
    if (ec) fs::remove(part, ec);
    return !ec;
}

std::optional<Copie> lire(const std::string& texteSysteme) {
    if (normalise(texteSysteme).empty()) return std::nullopt;
    std::error_code ec;
    if (!fs::exists(chemin(), ec)) return std::nullopt;
    auto r = hmi::pkg::readFile(chemin());
    if (!r) return std::nullopt;
    Copie c;
    c.paquet = std::move(r).value();
    // "copie-fenetre <pid> <empreinte> <vue>"
    const std::string& d = c.paquet.manifest.description;
    if (d.rfind(std::string(kMot) + " ", 0) != 0) return std::nullopt;
    std::size_t at = std::string(kMot).size() + 1;
    const std::size_t sp = d.find(' ', at);
    if (sp == std::string::npos) return std::nullopt;
    c.pid = std::strtol(d.substr(at, sp - at).c_str(), nullptr, 10);
    at = sp + 1;
    const std::size_t sp2 = d.find(' ', at);
    c.cle = d.substr(at, sp2 == std::string::npos ? std::string::npos : sp2 - at);
    if (sp2 != std::string::npos) c.vue = d.substr(sp2 + 1);
    if (c.cle != empreinte(texteSysteme)) return std::nullopt;   // une autre copie depuis
    if (!vueDe(c)) return std::nullopt;
    c.projet = c.paquet.manifest.fromProject;
    return c;
}

std::size_t objetsDe(const Copie& c) {
    const auto* v = vueDe(c);
    if (!v) return 0;
    std::set<Id> ids;
    for (const auto& o : v->objects) ids.insert(o.id);
    return static_cast<std::size_t>(std::count_if(v->objects.begin(), v->objects.end(),
                                                  [&](const hmi::Object& o) { return !ids.count(o.parent); }));
}

std::optional<Etendue> etendue(const Copie& c) {
    const auto* v = vueDe(c);
    if (!v || v->objects.empty()) return std::nullopt;
    std::set<Id> ids;
    for (const auto& o : v->objects) ids.insert(o.id);
    double x0 = 1e18, y0 = 1e18, x1 = -1e18, y1 = -1e18;
    for (const auto& o : v->objects) {
        if (ids.count(o.parent)) continue;
        const double x = o.number("x"), y = o.number("y");
        x0 = std::min(x0, x);
        y0 = std::min(y0, y);
        x1 = std::max(x1, x + std::max(0.0, o.number("w")));
        y1 = std::max(y1, y + std::max(0.0, o.number("h")));
    }
    if (x0 > x1 || y0 > y1) return std::nullopt;
    return Etendue{x0, y0, x1 - x0, y1 - y0};
}

Resultat coller(hmi::Project& p, Id into, const Copie& c, double dx, double dy, Id insideGroup) {
    Resultat out;
    if (!p.view(into) || !vueDe(c)) return out;
    // Ce qui manque est ajoute ; ce que le projet a deja (meme nom), le sien est garde.
    auto plan = hmi::pkg::plan(p, c.paquet);
    for (auto& it : plan.items)
        if (it.state != hmi::pkg::State::Missing) it.choice = hmi::pkg::Choice::KeepOurs;
    const auto res = hmi::pkg::importInto(p, c.paquet, plan);
    const std::string passage = std::string(" ") + kVue;
    const auto sansPassage = [&](const std::vector<std::string>& in, std::vector<std::string>& to) {
        for (const auto& s : in)
            if (s.size() < passage.size() || s.compare(s.size() - passage.size(), passage.size(), passage) != 0) to.push_back(s);
    };
    sansPassage(res.added, out.ajoutes);
    sansPassage(res.kept, out.gardes);
    const Id clipId = res.views.empty() ? kNoId : res.views.front();
    const hmi::View* clipView = p.view(clipId);
    if (!clipView) return out;
    const hmi::View clip = *clipView;   // une copie : la vue de passage s'en va
    std::erase_if(p.views, [&](const hmi::View& v) { return v.id == clipId; });
    hmi::View* v = p.view(into);
    if (!v) return out;
    // Les calques par leur nom (sinon le calque actif de la vue).
    const auto calque = [&](Id layer) {
        if (const auto* l = clip.layer(layer))
            for (const auto& ours : v->layers)
                if (ours.name == l->name) return ours.id;
        return v->activeLayer;
    };
    std::map<Id, Id> ids;
    for (const auto& o : clip.objects) ids[o.id] = p.allocate();
    for (auto o : clip.objects) {
        const bool haut = !ids.count(o.parent);
        o.id = ids[o.id];
        o.parent = haut ? insideGroup : ids[o.parent];
        o.layer = calque(o.layer);
        o.locked = false;
        if (!libre(*v, o.name)) {
            std::string base = o.name;
            while (!base.empty() && std::isdigit(static_cast<unsigned char>(base.back()))) base.pop_back();
            if (!base.empty() && base.back() == '_') base.pop_back();
            o.name = hmi::uniqueObjectName(*v, base.empty() ? "Objet" : base);
        }
        if (haut) {
            o.setNumber("x", o.number("x") + dx);
            o.setNumber("y", o.number("y") + dy);
            out.poses.push_back(o.id);
        }
        ++out.objets;
        v->objects.push_back(std::move(o));
    }
    hmi::edit::refreshGroupBounds(*v);
    return out;
}

std::string message(const Copie& c, const Resultat& r) {
    std::string m = std::to_string(r.poses.size()) + (r.poses.size() == 1 ? " objet coll\xC3\xA9" : " objets coll\xC3\xA9s");
    m += " depuis l'autre fen\xC3\xAAtre";
    if (!c.projet.empty()) m += " (\xC2\xAB " + c.projet + " \xC2\xBB" + (c.vue.empty() ? std::string{} : ", " + c.vue) + ")";
    const auto liste = [](const std::vector<std::string>& v) {
        std::string s;
        const std::size_t n = std::min<std::size_t>(v.size(), 4);
        for (std::size_t i = 0; i < n; ++i) s += (i ? ", " : "") + v[i];
        if (v.size() > n) s += " et " + std::to_string(v.size() - n) + " de plus";
        return s;
    };
    if (!r.ajoutes.empty()) m += " \xE2\x80\x94 ajout\xC3\xA9 ici : " + liste(r.ajoutes);
    if (!r.gardes.empty()) m += " \xE2\x80\x94 d\xC3\xA9j\xC3\xA0 l\xC3\xA0 (celui du projet est gard\xC3\xA9) : " + liste(r.gardes);
    return m + ". Ctrl+Z annule tout.";
}

} // namespace app::partage
