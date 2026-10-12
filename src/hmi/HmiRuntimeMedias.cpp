// hmi/HmiRuntimeMedias.cpp - le moteur de l'IHM : la video, le texte defilant, le code QR (1.12.12, lot H).
#include "HmiRuntime.hpp"

#include "HmiAlarmViews.hpp"   // inGroup
#include "HmiMarkers.hpp"
#include "HmiMarquee.hpp"
#include "HmiQr.hpp"
#include "HmiVideo.hpp"

#include <cctype>
#include <cmath>

namespace hmi {

namespace {

std::string trimmedOf(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

} // namespace

void Runtime::mediasReset() {
    videos_.clear();
    marquees_.clear();
    qrTold_.clear();
}

const video::Playback* Runtime::videoPlayback(Id object) const {
    const auto it = videos_.find(object);
    return it == videos_.end() ? nullptr : &it->second;
}

const std::vector<std::string>* Runtime::marqueeMessages(Id object) const {
    const auto it = marquees_.find(object);
    return it == marquees_.end() ? nullptr : &it->second;
}

void Runtime::mediasOpenView(const View& v, double now) {
    for (const auto& so : v.objects) {
        if (so.kind != Kind::Video) continue;
        // Le depart et la repetition tels que l'ecran les montre (leurs formules evaluees).
        Object o = so;
        evaluateProps(v.id, o);
        video::openView(videos_[o.id], video::settingsOf(o.text("start"), o.text("play")), now);
    }
}

// Les messages d'une source : un element par indice (une LIST, un tableau, une MAP : ses valeurs ; un
// joker : la formule de chaque element ; une enumeration : ses textes) ; sinon sa valeur, un message.
std::vector<std::string> Runtime::marqueeSource(std::string_view from, Id view) const {
    std::vector<std::string> out;
    sources::Joker joker;
    if (const auto j = sources::jokerOf(from)) joker = *j;
    else joker.before = std::string(from);
    if (const auto src = jokerSource(sources::sourceName(joker), view)) {
        for (std::size_t i = 0; i < src->indices.size(); ++i) {
            if (src->enumeration && trimmedOf(joker.after).empty()) {
                out.push_back(i < src->labels.size() ? src->labels[i] : src->indices[i]);
                continue;
            }
            if (auto t = jokerText(sources::itemAt(joker, src->indices[i], src->enumeration), view)) out.push_back(std::move(*t));
        }
        return out;
    }
    if (auto t = jokerText(from, view)) out.push_back(std::move(*t));
    return out;
}

void Runtime::mediasCycle(double now) {
    if (!project_) return;
    const auto visit = [&](Id viewId) {
        const View* v = viewOf(viewId);
        if (!v) return;
        for (const auto& so : v->objects) {
            switch (so.kind) {
                case Kind::Video: {
                    Object o = so;
                    evaluateProps(viewId, o);      // tel que l'ecran le montre
                    const auto settings = video::settingsOf(o.text("start"), o.text("play"));
                    auto it = videos_.find(so.id);
                    if (it == videos_.end()) {
                        it = videos_.emplace(so.id, video::Playback{}).first;
                        video::openView(it->second, settings, now);
                    }
                    auto& pb = it->second;
                    // La Lecture et la condition : leur formule (=Marche) ou leur texte (Marche) ; vide : elle joue. Une
                    // Lecture constante (TRUE, FAUX, NON, 0) se lit comme Generer la lit (FAUX : en pause).
                    bool play = true;
                    const Prop* playProp = so.find("playing");
                    const std::string fixedPlay = playProp && playProp->expr.empty() ? trimmedOf(markers::strip(playProp->value)) : std::string{};
                    if (!fixedPlay.empty() && roles::readBool(fixedPlay, true) == roles::readBool(fixedPlay, false))
                        play = roles::readBool(fixedPlay, true);
                    else
                        play = objectCondition(viewId, so, "playing", true);
                    const bool condition = settings.onCondition && objectCondition(viewId, so, "condition", false);
                    const auto* res = project_->resourceByName(roles::nameOf(o.text("video")));
                    const bool wasEnded = pb.ended;
                    video::step(pb, settings, now, play, condition, res ? res->seconds : 0.0);
                    if (pb.ended && !wasEnded) log("Vid\xC3\xA9o", where(*v, &so), "fin de la lecture (une fois)");
                    // La position dans sa variable, au dixieme de seconde, quand elle change. Refusee (une variable
                    // inconnue, une structure) : la Console le dit une fois, puis plus d'essai jusqu'a la prochaine
                    // marche (pas une erreur a chaque cycle) - qrTold_ garde les objets deja dits.
                    const std::string target = trimmedOf(markers::strip(o.text("positionVariable"), markers::Mode::Expression));
                    if (!target.empty() && !qrTold_.count(so.id)) {
                        const double pos = std::round(pb.position * 10.0) / 10.0;
                        if (pos != pb.written) {
                            const auto aliases = viewAliases(viewId);
                            if (writeConverted(target, sim::Value::real(pos), where(*v, &so), &so)) pb.written = pos;
                            else qrTold_.insert(so.id);
                        }
                    }
                    break;
                }
                case Kind::Marquee: {
                    const Prop* fromProp = so.find(marquee::kMessagesFromKey);
                    if (!fromProp || (fromProp->expr.empty() && trimmedOf(fromProp->value).empty())) {
                        marquees_.erase(so.id);
                        break;
                    }
                    Object o = so;
                    evaluateProps(viewId, o);
                    const std::string from = roles::nameOf(o.text(marquee::kMessagesFromKey));
                    if (from.empty()) {
                        marquees_.erase(so.id);
                        break;
                    }
                    std::vector<std::string> messages;
                    if (marquee::isAlarmSource(from)) {
                        // Les alarmes en cours (actives), dans l'ordre du bandeau ; de son groupe s'il en a un.
                        const std::string group = roles::nameOf(o.text("group"));
                        for (const auto& a : alarms_)
                            if (a.active && inGroup(a, group)) messages.push_back(a.message.empty() ? a.name : a.message);
                    } else {
                        messages = marqueeSource(from, viewId);
                    }
                    marquees_[so.id] = std::move(messages);
                    break;
                }
                case Kind::QrCode: {
                    // Un texte calcule (une formule, un texte a trous) trop long : dit une fois a la Console.
                    const Prop* tp = so.find("text");
                    if (!tp || qrTold_.count(so.id)) break;
                    if (tp->expr.empty() && tp->value.find('{') == std::string::npos) break;   // fixe : Generer l'a dit
                    std::string text;
                    if (!tp->expr.empty()) {
                        text = shownText(viewId, so, "text");
                    } else {
                        const auto aliases = viewAliases(viewId);
                        text = TextTemplate::compile(tp->value).render(environment());
                    }
                    const std::string level = shownText(viewId, so, "ecLevel", "M");
                    const char lv = level.empty() ? 'M' : level[0];
                    const auto room = qrRoom(text, lv);
                    if (room.fits || room.max <= 0) break;
                    qrTold_.insert(so.id);
                    logAt(LogLevel::Warning, "Code QR", where(*v, &so),
                          "texte " + qrRoomText(room, lv) + " : l'objet montre \xC2\xAB trop long \xC2\xBB (plus petit texte, ou correction plus faible)");
                    break;
                }
                default:
                    break;
            }
        }
    };
    visit(current_);
    for (const Id p : popups_) visit(p);
}

} // namespace hmi
