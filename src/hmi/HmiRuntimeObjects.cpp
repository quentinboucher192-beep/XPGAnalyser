// =============================================================================
//  hmi/HmiRuntimeObjects.cpp - 1.27.0 : LES TYPES DE L'IHM DANS LES SCRIPTS
// -----------------------------------------------------------------------------
//  Le client (06/11) : << dans les fonctions natives, il faut ajouter des
//  fonctions sur les objets [...] EditObject(Tableau_1.Width, 200) [...]
//  CopyObjectTo(Tableau_1, Tableau_2, {...}) avec si vecteur vide on copie tout
//  sauf le nom / MoveObject(X,Y), ResizeObject(W,H), pouvoir faire des listes
//  d'objets sur une vue, et faire des fonctions AlignTop(LIST*), Justify___,
//  hideIf, showIf [...] ajouter aussi les types 'Font', 'Object', 'Texture',
//  'Sound' [...] getResource(string) : Sound* >> ; et le type 'Style'.
//
//  UNE POIGNEE (sim::handleType) designe un objet d'une vue ("Vue.Objet") ou une
//  ressource ("@Sound:Bip court"). Ici : la trouver par son nom, lire ses
//  attributs, et les natives qui agissent.
//
//  OU CA AGIT : la vue MONTREE (la simulation, le poste), comme une affectation
//  Vue.Objet.X := 120 - la propriete est figee (overrides_) jusqu'au redemarrage
//  ou a ResetObject. Le projet ne change jamais.
//
//  L'ORDRE DE DESSIN (BringToFront...) et le calque (SetLayer) sont des cles du
//  moteur dans overrides_ ("@order", "@layer") ; un objet desactive par un
//  script porte "@disabled" (objectAllowed le refuse).
// =============================================================================
#include "HmiRuntime.hpp"

#include "HmiFontLibrary.hpp"
#include "HmiMedia.hpp"
#include "HmiPublicVars.hpp"
#include "HmiSemanticColors.hpp"
#include "HmiSoundLibrary.hpp"
#include "HmiStyles.hpp"
#include "HmiTrueType.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <memory>

namespace hmi {

namespace {

using HArg = sim::Environment::HandleArg;
using HRes = sim::Environment::HandleResult;

std::string upperText(std::string_view s) {
    std::string out(s);
    for (auto& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

// Le texte d'une valeur, tel qu'une propriete le garde.
std::string valueText(const sim::Value& v) {
    switch (v.type()) {
        case sim::Type::String: return v.asString();
        case sim::Type::Bool: return v.isTruthy() ? "TRUE" : "FALSE";
        case sim::Type::Real: return formatNumber(v.asReal());
        default: return v.display();
    }
}

double numberOf(const sim::Value& v) {
    if (v.type() == sim::Type::Real) return v.asReal();
    if (v.type() == sim::Type::String) {
        double d = 0;
        return parseNumber(v.asString(), d) ? d : 0.0;
    }
    if (v.type() == sim::Type::Bool) return v.isTruthy() ? 1.0 : 0.0;
    return static_cast<double>(v.asInteger());
}

// Un motif * et ? (sans casse).
bool wildcard(std::string_view pattern, std::string_view text) {
    std::size_t p = 0, t = 0, star = std::string_view::npos, mark = 0;
    const auto eq = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
    };
    while (t < text.size()) {
        if (p < pattern.size() && (pattern[p] == '?' || eq(pattern[p], text[t]))) {
            ++p;
            ++t;
        } else if (p < pattern.size() && pattern[p] == '*') {
            star = p++;
            mark = t;
        } else if (star != std::string_view::npos) {
            p = star + 1;
            t = ++mark;
        } else {
            return false;
        }
    }
    while (p < pattern.size() && pattern[p] == '*') ++p;
    return p == pattern.size();
}

// Le premier caractere d'un texte UTF-8, son code.
std::uint32_t firstCode(std::string_view s) {
    if (s.empty()) return 0;
    const auto b = [&](std::size_t i) { return static_cast<unsigned char>(s[i]); };
    const unsigned char c = b(0);
    if (c < 0x80) return c;
    if ((c >> 5) == 0x6 && s.size() >= 2) return ((c & 0x1Fu) << 6) | (b(1) & 0x3Fu);
    if ((c >> 4) == 0xE && s.size() >= 3) return ((c & 0x0Fu) << 12) | ((b(1) & 0x3Fu) << 6) | (b(2) & 0x3Fu);
    if ((c >> 3) == 0x1E && s.size() >= 4)
        return ((c & 0x07u) << 18) | ((b(1) & 0x3Fu) << 12) | ((b(2) & 0x3Fu) << 6) | (b(3) & 0x3Fu);
    return c;
}
// Les codes d'un texte UTF-8.
std::vector<std::uint32_t> codesOf(std::string_view s) {
    std::vector<std::uint32_t> out;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        const std::size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 1;
        out.push_back(firstCode(s.substr(i, n)));
        i += n;
    }
    return out;
}

// Une police lue une fois (par son contenu).
std::shared_ptr<const ttf::Font> parsedFont(const Resource& r) {
    static std::map<const void*, std::shared_ptr<const ttf::Font>> cache;
    if (!r.data || r.data->empty()) return nullptr;
    const void* key = r.data.get();
    if (const auto it = cache.find(key); it != cache.end()) return it->second;
    auto f = std::make_shared<ttf::Font>();
    if (!ttf::read(*r.data, *f)) f.reset();
    if (cache.size() > 64) cache.clear();
    cache[key] = f;
    return f;
}

// Le nom d'une ressource, avec ou sans son extension.
bool sameResourceName(const Resource& r, std::string_view name) {
    if (pub::same(r.name, name)) return true;
    const auto dot = r.name.rfind('.');
    return dot != std::string::npos && pub::same(std::string_view(r.name).substr(0, dot), name);
}

const Resource* resourceOf(const Project& p, MediaKind kind, std::string_view name) {
    for (const auto& r : p.assets.resources)
        if (r.kind() == kind && sameResourceName(r, name)) return &r;
    return nullptr;
}

const soundlib::Sound* nativeSound(std::string_view name) {
    for (const auto& s : soundlib::builtIn())
        if (pub::same(s.label, name) || pub::same(s.id, name)) return &s;
    return nullptr;
}

constexpr const char* kNames[] = {
    // les objets (38)
    "GETOBJECT", "OBJECTSOF", "OBJECTSIN", "OBJECTSOFKIND",
    "EDITOBJECT", "COPYOBJECTTO", "RESETOBJECT", "OBJECTPROP",
    "MOVEOBJECT", "MOVEBY", "RESIZEOBJECT", "RESIZEBY", "PLACEOBJECT",
    "ALIGNTOP", "ALIGNBOTTOM", "ALIGNLEFT", "ALIGNRIGHT", "ALIGNCENTERH", "ALIGNCENTERV", "ALIGNCENTER",
    "JUSTIFYH", "JUSTIFYV", "SAMEWIDTH", "SAMEHEIGHT", "SAMESIZE", "ARRANGEGRID",
    "SHOWIF", "HIDEIF", "ENABLEIF", "BLINKIF", "SHOW", "HIDE", "ENABLE", "DISABLE",
    "BRINGTOFRONT", "SENDTOBACK", "BRINGFORWARD", "SETLAYER",
    // les ressources (16)
    "GETRESOURCE", "GETFONT", "GETTEXTURE", "GETSOUND", "GETSTYLE", "FONTS", "TEXTURES", "SOUNDS", "STYLES",
    "TEXTWIDTH", "HASGLYPH", "PLAY", "STOP", "APPLYSTYLE", "STYLEVALUE", "COLOR",
};

} // namespace

const std::vector<std::string_view>& Runtime::handleNativeNames() {
    static const std::vector<std::string_view> k(std::begin(kNames), std::end(kNames));
    return k;
}

bool Runtime::isHandleNative(std::string_view u) const {
    const auto& names = handleNativeNames();
    if (std::find(names.begin(), names.end(), u) == names.end()) return false;
    // Une fonction du projet du meme nom passe avant (un projet qui avait sa fonction Show).
    if (project_) {
        for (const auto& f : project_->programs.functions)
            if (pub::same(f.name, u)) return false;
    }
    return true;
}

// =================================================================== trouver ==
namespace {
struct Located {
    Id            view{kNoId};      // la vue du projet (ou s'ecrivent les proprietes figees)
    const View*   shown{nullptr};   // la vue telle qu'elle tourne (composee)
    const Object* object{nullptr};
    std::string   path;             // "Vue.Objet"
};
} // namespace

std::string Runtime::shortObjectPath(std::string_view path) const {
    if (!project_ || path.empty()) return {};
    const auto cut = path.find_first_of(".[");
    const std::string_view head = path.substr(0, cut);
    if (pub::isSysRoot(head) || pub::viewNamed(*project_, head)) return {};
    const Id vid = handleView_ != kNoId ? handleView_ : current_;
    const View* v = const_cast<Runtime*>(this)->viewOf(vid);
    if (!v || !v->objectByName(head)) return {};
    return v->name + "." + std::string(path);
}

bool Runtime::handleOf(std::string_view name, std::string& type, std::string& path) {
    if (!project_ || name.empty()) return false;
    std::string t, n;
    if (sim::splitResourcePath(name, t, n)) {
        std::string why;
        if (!handleTarget(t, n, path, why)) return false;
        type = t;
        return true;
    }
    std::string why;
    if (!handleTarget("Object", name, path, why)) return false;
    type = "Object";
    return true;
}

bool Runtime::handleTarget(std::string_view type, std::string_view text, std::string& path, std::string& why) {
    if (!project_) {
        why = "pas de projet";
        return false;
    }
    const std::string kind = sim::handleTypeName(type);
    if (kind == "Object") {
        std::string_view name = text;
        // This : l'objet dont l'action tourne.
        if (pub::same(name, "This") || pub::same(name, "SELF")) {
            const Id vid = handleView_ != kNoId ? handleView_ : current_;
            const View* v = viewOf(vid);
            const Object* o = v && handleThis_ != kNoId ? v->object(handleThis_) : nullptr;
            if (!o) {
                why = "This : aucun objet ici (This d\xC3\xA9signe l'objet dont l'action tourne)";
                return false;
            }
            path = v->name + "." + o->name;
            return true;
        }
        const auto dot = name.find('.');
        if (dot != std::string_view::npos) {
            if (const View* pv = pub::viewNamed(*project_, name.substr(0, dot))) {
                const View* v = viewOf(pv->id);
                if (const Object* o = v ? v->objectByName(name.substr(dot + 1)) : nullptr) {
                    path = pv->name + "." + o->name;
                    return true;
                }
                why = "aucun objet '" + std::string(name.substr(dot + 1)) + "' dans " + pv->name;
                return false;
            }
        }
        const Id vid = handleView_ != kNoId ? handleView_ : current_;
        const View* v = viewOf(vid);
        if (const Object* o = v ? v->objectByName(name) : nullptr) {
            path = v->name + "." + o->name;
            return true;
        }
        why = "aucun objet '" + std::string(name) + "'" + (v ? " dans " + v->name : std::string{});
        return false;
    }
    if (kind == "Font") {
        if (const Resource* r = fontlib::resolve(*project_, text)) {
            path = sim::resourcePath("Font", r->name);
            return true;
        }
        why = "aucune police '" + std::string(text) + "' dans le projet (Ressources \xE2\x80\xBA Biblioth\xC3\xA8que de polices : l'importer)";
        return false;
    }
    if (kind == "Texture") {
        if (const Resource* r = resourceOf(*project_, MediaKind::Image, text)) {
            path = sim::resourcePath("Texture", r->name);
            return true;
        }
        why = "aucune image '" + std::string(text) + "' dans les ressources du projet";
        return false;
    }
    if (kind == "Sound") {
        if (const Resource* r = resourceOf(*project_, MediaKind::Sound, text)) {
            path = sim::resourcePath("Sound", r->name);
            return true;
        }
        if (const soundlib::Sound* s = nativeSound(text)) {
            path = sim::resourcePath("Sound", s->label);
            return true;
        }
        why = "aucun son '" + std::string(text) + "' (ni dans les ressources, ni dans la biblioth\xC3\xA8que de sons)";
        return false;
    }
    if (kind == "Style") {
        if (const Style* s = styles::find(*project_, text)) {
            path = sim::resourcePath("Style", s->name);
            return true;
        }
        why = "aucun style nomm\xC3\xA9 '" + std::string(text) + "'";
        return false;
    }
    why = std::string(type) + " n'est pas un type de l'IHM (Object, Font, Texture, Sound, Style)";
    return false;
}

// ================================================================ les membres ==
bool Runtime::handleMemberRead(std::string_view path, sim::Value& out) {
    if (!project_) return false;
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return false;
    const std::string_view base = path.substr(0, dot), member = path.substr(dot + 1);
    std::string type, name;
    if (sim::splitResourcePath(base, type, name)) {
        const auto text = [&](std::string s) { out = sim::Value::text(std::move(s)); return true; };
        const auto dint = [&](long long n) { out = sim::Value::integer(sim::Type::DInt, n); return true; };
        if (pub::same(member, "Name")) return text(name);
        if (type == "Font") {
            const Resource* r = fontlib::resolve(*project_, name);
            if (!r) return false;
            const auto f = parsedFont(*r);
            if (pub::same(member, "Family")) return text(f ? f->family : r->detail);
            if (pub::same(member, "Style")) return text(f ? ttf::styleLabel(*f) : std::string{});
            if (pub::same(member, "Glyphs")) return dint(f ? static_cast<long long>(f->glyphs.size()) : 0);
            if (pub::same(member, "Format")) return text(r->format);
            return false;
        }
        if (type == "Texture") {
            const Resource* r = resourceOf(*project_, MediaKind::Image, name);
            if (!r) return false;
            if (pub::same(member, "Width")) return dint(r->width);
            if (pub::same(member, "Height")) return dint(r->height);
            if (pub::same(member, "Format")) return text(r->format);
            return false;
        }
        if (type == "Sound") {
            double seconds = 0;
            if (const Resource* r = resourceOf(*project_, MediaKind::Sound, name)) seconds = r->seconds;
            else if (const soundlib::Sound* s = nativeSound(name)) seconds = s->seconds;
            if (pub::same(member, "Duration")) { out = sim::Value::real(seconds); return true; }
            if (pub::same(member, "Playing")) {
                const auto it = soundStarts_.find(upperText(name));
                out = sim::Value::boolean(it != soundStarts_.end() && now_ - it->second < std::max(0.05, seconds));
                return true;
            }
            if (pub::same(member, "Volume")) {
                out = sim::Value::real(soundVolume(settings_, SoundChannel::Scripts) / 100.0);
                return true;
            }
            return false;
        }
        if (type == "Style") {
            const Style* s = styles::find(*project_, name);
            if (!s) return false;
            if (pub::same(member, "Folder")) return text(s->folder);
            if (pub::same(member, "Builtin")) { out = sim::Value::boolean(s->builtin); return true; }
            if (pub::same(member, "Description")) return text(s->description);
            return false;
        }
        return false;
    }
    // Un objet : ce que les variables publiques n'ont pas.
    const auto odot = base.find('.');
    if (odot == std::string_view::npos) return false;
    const View* pv = pub::viewNamed(*project_, base.substr(0, odot));
    const View* v = pv ? viewOf(pv->id) : nullptr;
    const Object* o = v ? v->objectByName(base.substr(odot + 1)) : nullptr;
    if (!o) return false;
    if (pub::same(member, "Kind")) { out = sim::Value::text(std::string(kindLabel(o->kind))); return true; }
    if (pub::same(member, "View")) { out = sim::Value::text(pv->name); return true; }
    if (pub::same(member, "Style") || pub::same(member, "NamedStyle")) {
        const std::string* w = overrideOf(pv->id, o->id, "namedStyle");
        out = sim::Value::text(w ? *w : o->text("namedStyle"));
        return true;
    }
    if (pub::same(member, "Parent")) {
        const Object* parent = o->parent != kNoId ? v->object(o->parent) : nullptr;
        out = sim::Value::text(parent ? pv->name + "." + parent->name : std::string{});
        return true;
    }
    if (pub::same(member, "Opacity") || pub::same(member, "Font") || pub::same(member, "Image")) {
        out = pub::same(member, "Opacity") ? sim::Value::real(1.0) : sim::Value::text(std::string{});
        return true;                               // l'objet n'a pas la propriete : sa valeur neutre
    }
    return false;
}

int Runtime::handleMemberWrite(std::string_view path, const sim::Value& value, std::string* why) {
    if (!project_) return -1;
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return -1;
    const std::string_view base = path.substr(0, dot), member = path.substr(dot + 1);
    const auto refuse = [&](std::string w) {
        if (why) *why = std::move(w);
        return 0;
    };
    std::string type, name;
    if (sim::splitResourcePath(base, type, name))
        return refuse(type + " '" + name + "' : ses attributs se lisent seulement (" + std::string(member) + ")");
    const auto odot = base.find('.');
    if (odot == std::string_view::npos) return -1;
    const View* pv = pub::viewNamed(*project_, base.substr(0, odot));
    const View* v = pv ? viewOf(pv->id) : nullptr;
    const Object* o = v ? v->objectByName(base.substr(odot + 1)) : nullptr;
    if (!o) return -1;
    const std::string where = pv->name + "." + o->name;
    if (pub::same(member, "Enabled")) {
        overrides_[{pv->id, o->id}]["@disabled"] = value.isTruthy() ? "FALSE" : "TRUE";
        log("Action", source_, where + ".Enabled = " + (value.isTruthy() ? std::string("TRUE") : std::string("FALSE")));
        return 1;
    }
    if (pub::same(member, "Layer")) {
        const std::string wanted = valueText(value);
        for (const auto& l : v->layers)
            if (pub::same(l.name, wanted)) {
                overrides_[{pv->id, o->id}]["@layer"] = std::to_string(l.id);
                log("Action", source_, where + ".Layer = " + l.name);
                return 1;
            }
        return refuse(where + ".Layer : aucun calque '" + wanted + "' dans " + pv->name);
    }
    if (pub::same(member, "Style") && !o->find("style")) {
        const std::string wanted = valueText(value);
        const Style* s = wanted.empty() ? nullptr : styles::find(*project_, wanted);
        if (!wanted.empty() && !s) return refuse(where + ".Style : aucun style nomm\xC3\xA9 '" + wanted + "'");
        if (s)
            for (const auto& prop : styles::valuesFor(*project_, *s, *o)) overrides_[{pv->id, o->id}][prop.key] = prop.value;
        overrides_[{pv->id, o->id}]["namedStyle"] = wanted;
        log("Action", source_, where + ".Style = " + wanted);
        return 1;
    }
    if (pub::same(member, "Kind") || pub::same(member, "View") || pub::same(member, "Parent") || pub::same(member, "Children"))
        return refuse(where + "." + std::string(member) + " : se lit seulement");
    return -1;
}

sim::ObjRef Runtime::handleList(std::string_view path) {
    if (!project_) return nullptr;
    if (const std::string full = shortObjectPath(path); !full.empty()) return handleList(full);   // Pompe_1.Children
    const auto dot = path.rfind('.');
    if (dot == std::string_view::npos) return nullptr;
    const std::string_view base = path.substr(0, dot), member = path.substr(dot + 1);
    const auto list = [&](const std::vector<std::string>& paths) {
        auto o = sim::makeObj(sim::listType(sim::handleType("Object")));
        for (const auto& p : paths) {
            auto h = sim::makeObj(sim::handleType("Object"));
            h->bound = true;
            h->targetName = p;
            o->items.push_back(h);
        }
        return o;
    };
    std::string type, name;
    if (sim::splitResourcePath(base, type, name)) {
        if (type != "Style" || !pub::same(member, "Users")) return nullptr;
        std::vector<std::string> paths;
        std::vector<Id> shown{current_};
        shown.insert(shown.end(), popups_.begin(), popups_.end());
        for (const Id id : shown) {
            const View* v = viewOf(id);
            if (!v) continue;
            for (const auto& o : v->objects)
                if (pub::same(o.text("namedStyle"), name)) paths.push_back(v->name + "." + o.name);
        }
        return list(paths);
    }
    if (!pub::same(member, "Children")) return nullptr;
    const auto odot = base.find('.');
    if (odot == std::string_view::npos) return nullptr;
    const View* pv = pub::viewNamed(*project_, base.substr(0, odot));
    const View* v = pv ? viewOf(pv->id) : nullptr;
    const Object* o = v ? v->objectByName(base.substr(odot + 1)) : nullptr;
    if (!o) return nullptr;
    std::vector<std::string> paths;
    for (const Id c : v->childrenOf(o->id))
        if (const Object* child = v->object(c)) paths.push_back(pv->name + "." + child->name);
    return list(paths);
}

// ================================================================ les natives ==
bool Runtime::handleNative(std::string_view u, const std::vector<HArg>& args, HRes& out, std::string& why) {
    if (!project_) {
        why = "pas de projet";
        return false;
    }
    const std::string fn(u);
    const auto fail = [&](std::string w) {
        why = std::move(w);
        return false;
    };
    const auto value = [&](std::size_t i) -> const sim::Value* { return i < args.size() && args[i].isValue ? &args[i].value : nullptr; };
    const auto text = [&](std::size_t i) { const auto* v = value(i); return v ? valueText(*v) : std::string{}; };
    const auto number = [&](std::size_t i, double fallback) { const auto* v = value(i); return v ? numberOf(*v) : fallback; };
    // La vue ou on cherche par defaut.
    const Id baseView = handleView_ != kNoId ? handleView_ : current_;

    // ---- les objets d'un argument ----
    const auto locate = [&](const std::string& path, Located& at) {
        const auto d = path.find('.');
        if (d == std::string::npos) return false;
        const View* pv = pub::viewNamed(*project_, std::string_view(path).substr(0, d));
        const View* v = pv ? viewOf(pv->id) : nullptr;
        const Object* o = v ? v->objectByName(std::string_view(path).substr(d + 1)) : nullptr;
        if (!o) return false;
        at = Located{pv->id, v, o, pv->name + "." + o->name};
        return true;
    };
    const auto objectsOf = [&](std::size_t i, std::vector<Located>& found, bool allowEmpty = false) {
        if (i >= args.size()) return fail(fn + " : l'objet (ou la liste d'objets) manque");
        const HArg& a = args[i];
        if (a.isValue && a.value.type() == sim::Type::String) {      // un nom : 'Pompe_1', 'Vue.Pompe_1'
            std::string p, w;
            if (!handleTarget("Object", a.value.asString(), p, w)) return fail(fn + " : " + w);
            Located at;
            if (locate(p, at)) found.push_back(at);
            return true;
        }
        for (const auto& [type, p] : a.handles) {
            if (type != "Object") return fail(fn + " : un " + type + " n'est pas un objet");
            if (p.empty()) return fail(fn + " : objet NULL (il ne d\xC3\xA9signe rien)");
            Located at;
            if (!locate(p, at)) return fail(fn + " : '" + p + "' n'est pas (ou plus) dans la vue montr\xC3\xA9" "e");
            found.push_back(at);
        }
        if (a.handles.empty() && !a.isList && !allowEmpty) {
            if (!a.place.empty()) return fail(fn + " : '" + a.place + "' n'est pas un objet");
            return fail(fn + " : un objet (ou une liste d'objets) est attendu");
        }
        return true;
    };
    const auto read = [&](const Located& at, std::string_view member) {
        sim::Value v;
        return publicRead(at.path + "." + std::string(member), v) ? numberOf(v) : 0.0;
    };
    const auto write = [&](const Located& at, std::string_view member, const sim::Value& v) {
        std::string w;
        const int r = publicWrite(at.path + "." + std::string(member), v, &w);
        if (r != 1) {
            why = fn + " : " + (w.empty() ? at.path + "." + std::string(member) + " ne s'\xC3\xA9" "crit pas" : w);
            return false;
        }
        return true;
    };
    const auto setNumber = [&](const Located& at, std::string_view member, double x) {
        return write(at, member, sim::Value::real(std::round(x * 1000.0) / 1000.0));
    };
    struct Geo { double x, y, w, h; };
    const auto geo = [&](const Located& at) { return Geo{read(at, "X"), read(at, "Y"), read(at, "Width"), read(at, "Height")}; };
    const auto done = [&](bool ok = true) {
        out = HRes{};
        out.value = sim::Value::boolean(ok);
        return true;
    };
    const auto handleOut = [&](HRes::Kind k, const std::string& type, std::vector<std::string> paths) {
        out = HRes{};
        out.kind = k;
        out.type = type;
        out.paths = std::move(paths);
        return true;
    };
    // Les objets d'une vue, dans l'ordre de dessin.
    const auto viewObjects = [&](Id vid, const std::function<bool(const View&, const Object&)>& keep) {
        std::vector<std::string> paths;
        const View* v = viewOf(vid);
        const View* pv = project_->view(vid);
        if (!v || !pv) return paths;
        for (const Object* o : v->paintOrder())
            if (o && keep(*v, *o)) paths.push_back(pv->name + "." + o->name);
        return paths;
    };

    // ======================================================== trouver ===
    if (fn == "GETOBJECT") {
        std::string p, w;
        if (!handleTarget("Object", text(0), p, w)) p.clear();
        return handleOut(HRes::Kind::Pointer, "Object", {p});
    }
    if (fn == "OBJECTSOF" || fn == "OBJECTSOFKIND") {
        std::string motif = text(0);
        if (fn == "OBJECTSOFKIND") motif = "type:" + motif;
        Id vid = baseView;
        // 'Vue_A.*' : une autre vue.
        if (const auto d = motif.find('.'); d != std::string::npos && motif.rfind("type:", 0) != 0 && motif.rfind("calque:", 0) != 0)
            if (const View* pv = pub::viewNamed(*project_, std::string_view(motif).substr(0, d))) {
                vid = pv->id;
                motif = motif.substr(d + 1);
            }
        const auto keep = [&](const View& v, const Object& o) {
            if (motif.rfind("type:", 0) == 0 || motif.rfind("TYPE:", 0) == 0) {
                const std::string k = motif.substr(5);
                return pub::same(kindLabel(o.kind), k) || pub::same(kindKey(o.kind), k);
            }
            if (motif.rfind("calque:", 0) == 0 || motif.rfind("CALQUE:", 0) == 0) {
                const Layer* l = v.layer(o.layer);
                return l && wildcard(motif.substr(7), l->name);
            }
            return wildcard(motif.empty() ? std::string("*") : motif, o.name);
        };
        return handleOut(HRes::Kind::List, "Object", viewObjects(vid, keep));
    }
    if (fn == "OBJECTSIN") {
        const double x = number(0, 0), y = number(1, 0), w = number(2, 0), h = number(3, 0);
        std::vector<std::string> paths;
        const View* v = viewOf(baseView);
        const View* pv = project_->view(baseView);
        if (v && pv)
            for (const Object* o : v->paintOrder()) {
                if (!o) continue;
                Located at{pv->id, v, o, pv->name + "." + o->name};
                const Geo g = geo(at);
                if (g.x < x + w && g.x + g.w > x && g.y < y + h && g.y + g.h > y) paths.push_back(at.path);
            }
        return handleOut(HRes::Kind::List, "Object", paths);
    }

    // ================================================== modifier, copier ===
    if (fn == "EDITOBJECT") {
        if (args.size() < 2) return fail("EditObject(Objet.Propri\xC3\xA9t\xC3\xA9, valeur) : deux arguments");
        std::string target = args[0].place;
        if (target.empty()) return fail("EditObject : le premier argument nomme une propri\xC3\xA9t\xC3\xA9 (Tableau_1.Width)");
        if (const std::string full = shortObjectPath(target); !full.empty()) target = full;
        sim::Value v;
        if (args[1].isValue) v = args[1].value;
        else if (!args[1].handles.empty()) {
            std::string t, n;
            const std::string& p = args[1].handles.front().second;
            v = sim::Value::text(sim::splitResourcePath(p, t, n) ? n : p);
        } else return fail("EditObject : la valeur manque");
        std::string w;
        const int r = publicWrite(target, v, &w);
        if (r != 1) return fail("EditObject : " + (w.empty() ? target + " ne s'\xC3\xA9" "crit pas" : w));
        return done();
    }
    if (fn == "COPYOBJECTTO") {
        std::vector<Located> from, to;
        if (!objectsOf(0, from) || !objectsOf(1, to)) return false;
        if (from.size() != 1) return fail("CopyObjectTo : un seul objet source");
        const Object& src = *from.front().object;
        std::vector<std::string> names;
        if (args.size() > 2) {
            for (const auto& m : args[2].members) {
                const auto d = m.rfind('.');
                names.push_back(d == std::string::npos ? m : m.substr(d + 1));
            }
        }
        for (const auto& dst : to) {
            std::vector<std::pair<std::string, std::string>> keys;     // (nom public, cle de la cible)
            if (names.empty()) {
                for (const auto& p : src.props)
                    if (pub::keyWritable(p.key) && dst.object->find(p.key)) keys.emplace_back(pub::publicName(p.key), p.key);
            } else {
                for (const auto& n : names) {
                    const std::string k = pub::keyOf(*dst.object, n);
                    if (k.empty()) return fail("CopyObjectTo : " + dst.path + " n'a pas de propri\xC3\xA9t\xC3\xA9 " + n);
                    keys.emplace_back(n, k);
                }
            }
            for (const auto& [publicName, key] : keys) {
                sim::Value v;
                if (!publicRead(from.front().path + "." + publicName, v)) continue;
                if (!write(dst, publicName, v)) return false;
            }
        }
        return done();
    }
    if (fn == "RESETOBJECT") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        for (const auto& at : list) overrides_.erase({at.view, at.object->id});
        composed_.clear();
        return done();
    }
    if (fn == "OBJECTPROP") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        if (list.empty()) return fail("ObjectProp : l'objet manque");
        sim::Value v;
        const std::string key = text(1);
        const std::string k = pub::keyOf(*list.front().object, key);
        if (!publicRead(list.front().path + "." + (k.empty() ? key : pub::publicName(k)), v)) {
            out = HRes{};
            out.value = sim::Value::text(std::string{});
            return true;
        }
        out = HRes{};
        out.value = sim::Value::text(valueText(v));
        return true;
    }

    // ================================================= position, taille ===
    if (fn == "MOVEOBJECT" || fn == "MOVEBY" || fn == "RESIZEOBJECT" || fn == "RESIZEBY" || fn == "PLACEOBJECT") {
        std::vector<Located> list;
        std::size_t first = 1;
        // MoveObject(X, Y) dans une action : l'objet lui-meme (This).
        if (!args.empty() && args[0].isValue && args[0].value.type() != sim::Type::String) {
            std::string p, w;
            if (!handleTarget("Object", "This", p, w)) return fail(fn + " : " + w);
            Located at;
            if (locate(p, at)) list.push_back(at);
            first = 0;
        } else if (!objectsOf(0, list)) {
            return false;
        }
        const double a = number(first, 0), b = number(first + 1, 0);
        for (const auto& at : list) {
            const Geo g = geo(at);
            bool ok = true;
            if (fn == "MOVEOBJECT") ok = setNumber(at, "X", a) && setNumber(at, "Y", b);
            else if (fn == "MOVEBY") ok = setNumber(at, "X", g.x + a) && setNumber(at, "Y", g.y + b);
            else if (fn == "RESIZEOBJECT") ok = setNumber(at, "Width", std::max(1.0, a)) && setNumber(at, "Height", std::max(1.0, b));
            else if (fn == "RESIZEBY") ok = setNumber(at, "Width", std::max(1.0, g.w + a)) && setNumber(at, "Height", std::max(1.0, g.h + b));
            else ok = setNumber(at, "X", a) && setNumber(at, "Y", b) && setNumber(at, "Width", std::max(1.0, number(first + 2, g.w)))
                   && setNumber(at, "Height", std::max(1.0, number(first + 3, g.h)));
            if (!ok) return false;
        }
        return done();
    }

    // =========================================================== aligner ===
    if (fn.rfind("ALIGN", 0) == 0) {
        std::vector<Located> list, ref;
        if (!objectsOf(0, list)) return false;
        if (args.size() > 1 && !objectsOf(1, ref)) return false;
        if (list.empty()) return done(false);
        std::vector<Geo> g;
        for (const auto& at : list) g.push_back(geo(at));
        double top = g[0].y, bottom = g[0].y + g[0].h, left = g[0].x, right = g[0].x + g[0].w;
        for (const auto& x : g) {
            top = std::min(top, x.y);
            bottom = std::max(bottom, x.y + x.h);
            left = std::min(left, x.x);
            right = std::max(right, x.x + x.w);
        }
        if (!ref.empty()) {
            const Geo r = geo(ref.front());
            top = r.y;
            bottom = r.y + r.h;
            left = r.x;
            right = r.x + r.w;
        }
        const double cx = (left + right) / 2.0, cy = (top + bottom) / 2.0;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const Geo& x = g[i];
            bool ok = true;
            if (fn == "ALIGNTOP") ok = setNumber(list[i], "Y", top);
            else if (fn == "ALIGNBOTTOM") ok = setNumber(list[i], "Y", bottom - x.h);
            else if (fn == "ALIGNLEFT") ok = setNumber(list[i], "X", left);
            else if (fn == "ALIGNRIGHT") ok = setNumber(list[i], "X", right - x.w);
            else if (fn == "ALIGNCENTERH") ok = setNumber(list[i], "X", cx - x.w / 2.0);
            else if (fn == "ALIGNCENTERV") ok = setNumber(list[i], "Y", cy - x.h / 2.0);
            else if (fn == "ALIGNCENTER") ok = setNumber(list[i], "X", cx - x.w / 2.0) && setNumber(list[i], "Y", cy - x.h / 2.0);
            if (!ok) return false;
        }
        return done();
    }

    // ================================================ repartir, egaliser ===
    if (fn == "JUSTIFYH" || fn == "JUSTIFYV") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        if (list.size() < 2) return done(!list.empty());
        const bool h = fn == "JUSTIFYH";
        std::vector<std::pair<Located, Geo>> items;
        for (const auto& at : list) items.emplace_back(at, geo(at));
        std::stable_sort(items.begin(), items.end(), [&](const auto& a, const auto& b) { return h ? a.second.x < b.second.x : a.second.y < b.second.y; });
        const bool gapGiven = value(1) != nullptr;
        double gap = number(1, 0);
        if (!gapGiven) {           // des ecarts egaux entre le premier et le dernier
            const auto& f = items.front().second;
            const auto& l = items.back().second;
            double sizes = 0;
            for (const auto& [at, g] : items) sizes += h ? g.w : g.h;
            gap = ((h ? l.x + l.w - f.x : l.y + l.h - f.y) - sizes) / static_cast<double>(items.size() - 1);
        }
        double pos = h ? items.front().second.x : items.front().second.y;
        for (const auto& [at, g] : items) {
            if (!setNumber(at, h ? "X" : "Y", pos)) return false;
            pos += (h ? g.w : g.h) + gap;
        }
        return done();
    }
    if (fn == "SAMEWIDTH" || fn == "SAMEHEIGHT" || fn == "SAMESIZE") {
        std::vector<Located> list, ref;
        if (!objectsOf(0, list)) return false;
        if (args.size() > 1 && !objectsOf(1, ref)) return false;
        if (list.empty()) return done(false);
        const Geo r = geo(ref.empty() ? list.front() : ref.front());
        for (const auto& at : list) {
            if (fn != "SAMEHEIGHT" && !setNumber(at, "Width", r.w)) return false;
            if (fn != "SAMEWIDTH" && !setNumber(at, "Height", r.h)) return false;
        }
        return done();
    }
    if (fn == "ARRANGEGRID") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        if (list.empty()) return done(false);
        const int columns = std::max(1, static_cast<int>(number(1, 1)));
        const double gap = number(2, 8);
        double cw = 0, ch = 0;
        std::vector<Geo> g;
        for (const auto& at : list) {
            g.push_back(geo(at));
            cw = std::max(cw, g.back().w);
            ch = std::max(ch, g.back().h);
        }
        const double x0 = g.front().x, y0 = g.front().y;
        for (std::size_t i = 0; i < list.size(); ++i) {
            const double col = static_cast<double>(static_cast<int>(i) % columns), row = static_cast<double>(static_cast<int>(i) / columns);
            if (!setNumber(list[i], "X", x0 + col * (cw + gap)) || !setNumber(list[i], "Y", y0 + row * (ch + gap))) return false;
        }
        return done();
    }

    // =================================================== visibilite, etat ===
    if (fn == "SHOWIF" || fn == "HIDEIF" || fn == "ENABLEIF" || fn == "BLINKIF" || fn == "SHOW" || fn == "HIDE" || fn == "ENABLE"
        || fn == "DISABLE") {
        std::vector<Located> list;
        if (!objectsOf(0, list, true)) return false;
        const bool conditional = fn.size() > 2 && fn.compare(fn.size() - 2, 2, "IF") == 0;
        bool c = true;
        if (conditional) {
            const sim::Value* v = value(1);
            if (!v) return fail(fn + "(objet, condition) : la condition (un BOOL) manque");
            c = v->isTruthy();
        }
        bool recompose = false;
        for (const auto& at : list) {
            if (fn == "SHOWIF" || fn == "SHOW" || fn == "HIDEIF" || fn == "HIDE") {
                const bool visible = (fn == "SHOWIF" || fn == "SHOW") ? c : !c;
                if (!write(at, "Visible", sim::Value::boolean(visible))) return false;
            } else if (fn == "BLINKIF") {
                // Le clignotement : une propriete que tout objet peut porter (meme sans l'avoir dans l'editeur).
                overrides_[{at.view, at.object->id}]["blink"] = c ? "TRUE" : "FALSE";
                log("Action", source_, at.path + ".Blink = " + (c ? std::string("TRUE") : std::string("FALSE")));
                recompose = true;
            } else {
                const bool enabled = fn == "DISABLE" ? false : c;
                overrides_[{at.view, at.object->id}]["@disabled"] = enabled ? "FALSE" : "TRUE";
                recompose = true;
            }
        }
        if (recompose) composed_.clear();
        out = HRes{};
        out.value = sim::Value::boolean(c);
        return true;
    }

    // =================================================== ordre, calques ===
    if (fn == "BRINGTOFRONT" || fn == "SENDTOBACK" || fn == "BRINGFORWARD" || fn == "SETLAYER") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        for (const auto& at : list) {
            auto& keys = overrides_[{at.view, at.object->id}];
            if (fn == "SETLAYER") {
                const std::string wanted = text(1);
                const Layer* found = nullptr;
                for (const auto& l : at.shown->layers)
                    if (pub::same(l.name, wanted)) found = &l;
                if (!found) return fail("SetLayer : aucun calque '" + wanted + "' dans " + at.shown->name);
                keys["@layer"] = std::to_string(found->id);
            } else if (fn == "BRINGFORWARD") {
                // Juste devant l'objet suivant de son calque.
                const auto order = at.shown->paintOrder();
                double next = 0;
                bool seen = false, have = false;
                for (std::size_t k = 0; k < order.size(); ++k) {
                    const Object* o = order[k];
                    if (!o) continue;
                    if (o->id == at.object->id) { seen = true; continue; }
                    if (seen && o->layer == at.object->layer) {
                        const Prop* p = o->find("@order");
                        double n = 0;
                        next = p && parseNumber(p->value, n) ? n : static_cast<double>(at.shown->indexOf(o->id));
                        have = true;
                        break;
                    }
                }
                if (have) keys["@order"] = formatNumber(next + 0.5);
            } else {
                orderSeq_ += 1;
                keys["@order"] = formatNumber(fn == "BRINGTOFRONT" ? 1e6 + orderSeq_ : -1e6 - orderSeq_);
            }
            log("Action", source_, at.path + " : " + fn);
        }
        composed_.clear();
        return done();
    }

    // ======================================================== ressources ===
    if (fn == "GETRESOURCE") {
        const std::string n = text(0);
        for (const char* type : {"Font", "Texture", "Sound", "Style"}) {
            std::string p, w;
            if (std::string(type) == "Font" && !resourceOf(*project_, MediaKind::Font, n)) continue;   // pas la police par defaut
            if (handleTarget(type, n, p, w)) return handleOut(HRes::Kind::Pointer, type, {p});
        }
        return handleOut(HRes::Kind::Pointer, "Sound", {std::string{}});
    }
    if (fn == "GETFONT" || fn == "GETTEXTURE" || fn == "GETSOUND" || fn == "GETSTYLE") {
        const std::string type = fn == "GETFONT" ? "Font" : fn == "GETTEXTURE" ? "Texture" : fn == "GETSOUND" ? "Sound" : "Style";
        std::string p, w;
        if (!handleTarget(type, text(0), p, w)) p.clear();
        return handleOut(HRes::Kind::Pointer, type, {p});
    }
    if (fn == "FONTS" || fn == "TEXTURES" || fn == "SOUNDS" || fn == "STYLES") {
        const std::string type = fn == "FONTS" ? "Font" : fn == "TEXTURES" ? "Texture" : fn == "SOUNDS" ? "Sound" : "Style";
        const std::string motif = value(0) ? text(0) : std::string("*");
        // 'Pictos/*' : un dossier ; sinon le nom.
        const auto matches = [&](const std::string& name, const std::string& folder) {
            if (const auto slash = motif.find('/'); slash != std::string::npos)
                return wildcard(motif.substr(0, slash), folder) && wildcard(motif.substr(slash + 1), name);
            return wildcard(motif, name);
        };
        std::vector<std::string> paths;
        if (type == "Style") {
            for (const Style* s : styles::all(*project_))
                if (s && matches(s->name, s->folder)) paths.push_back(sim::resourcePath("Style", s->name));
        } else {
            const MediaKind k = type == "Font" ? MediaKind::Font : type == "Texture" ? MediaKind::Image : MediaKind::Sound;
            for (const auto& r : project_->assets.resources)
                if (r.kind() == k && matches(r.name, r.folder)) paths.push_back(sim::resourcePath(type, r.name));
        }
        return handleOut(HRes::Kind::List, type, paths);
    }
    // Une ressource d'un argument : (type, nom).
    const auto resourceArg = [&](std::size_t i, const std::string& want, std::string& name) {
        if (i >= args.size()) return fail(fn + " : la ressource manque");
        const HArg& a = args[i];
        if (a.isValue && a.value.type() == sim::Type::String) {
            std::string p, w;
            if (!handleTarget(want, a.value.asString(), p, w)) return fail(fn + " : " + w);
            std::string t;
            return sim::splitResourcePath(p, t, name) || fail(fn + " : " + p);
        }
        if (a.handles.empty()) return fail(fn + " : un " + want + " est attendu");
        const auto& [type, p] = a.handles.front();
        if (p.empty()) return fail(fn + " : " + want + " NULL (il ne d\xC3\xA9signe rien)");
        std::string t;
        if (!sim::splitResourcePath(p, t, name) || t != want) return fail(fn + " : un " + want + " est attendu, pas " + p);
        return true;
    };
    if (fn == "TEXTWIDTH" || fn == "HASGLYPH") {
        std::string name;
        if (!resourceArg(0, "Font", name)) return false;
        const Resource* r = fontlib::resolve(*project_, name);
        const auto f = r ? parsedFont(*r) : nullptr;
        if (!f) return fail(fn + " : la police '" + name + "' ne se lit pas");
        out = HRes{};
        if (fn == "HASGLYPH") {
            out.value = sim::Value::boolean(ttf::glyphOf(*f, firstCode(text(1))) > 0);
            return true;
        }
        double units = 0;
        for (const std::uint32_t c : codesOf(text(1))) units += ttf::advanceOf(*f, ttf::glyphOf(*f, c));
        const double size = number(2, 14);
        out.value = sim::Value::real(f->unitsPerEm > 0 ? units * size / f->unitsPerEm : 0.0);
        return true;
    }
    if (fn == "PLAY" || fn == "STOP") {
        std::string name;
        if (fn == "STOP" && args.empty()) {
            if (hooks_.stopSounds) hooks_.stopSounds();
            soundStarts_.clear();
            return done();
        }
        if (!resourceArg(0, "Sound", name)) return false;
        if (fn == "STOP") {
            if (hooks_.stopSounds) hooks_.stopSounds();
            soundStarts_.erase(upperText(name));
            log("Action", source_, "son arr\xC3\xAAt\xC3\xA9 : " + name);
            return done();
        }
        const bool played = playSound(name, source_, SoundChannel::Scripts);
        if (played) soundStarts_[upperText(name)] = now_;
        return done(played);
    }
    if (fn == "APPLYSTYLE") {
        std::vector<Located> list;
        if (!objectsOf(0, list)) return false;
        std::string name;
        if (!resourceArg(1, "Style", name)) return false;
        const Style* s = styles::find(*project_, name);
        if (!s) return fail("ApplyStyle : aucun style nomm\xC3\xA9 '" + name + "'");
        for (const auto& at : list) {
            auto& keys = overrides_[{at.view, at.object->id}];
            for (const auto& prop : styles::valuesFor(*project_, *s, *at.object)) keys[prop.key] = prop.value;
            keys["namedStyle"] = s->name;
            log("Action", source_, at.path + " : style " + s->name);
        }
        composed_.clear();
        return done();
    }
    if (fn == "STYLEVALUE") {
        std::string name;
        if (!resourceArg(0, "Style", name)) return false;
        const Style* s = styles::find(*project_, name);
        std::string key = text(1), found;
        for (const auto& p : s ? s->props : std::vector<Prop>{})
            if (pub::same(p.key, key) || pub::same(pub::publicName(p.key), key)) found = p.value;
        if (found.empty() && s)
            for (const auto& t : s->targets)
                for (const auto& p : t.props)
                    if (found.empty() && (pub::same(p.key, key) || pub::same(pub::publicName(p.key), key))) found = p.value;
        out = HRes{};
        out.value = sim::Value::text(found);
        return true;
    }
    if (fn == "COLOR") {
        out = HRes{};
        out.value = sim::Value::text(semantic::resolve(*project_, text(0)));
        return true;
    }
    return fail(fn + " : native inconnue");
}

} // namespace hmi
