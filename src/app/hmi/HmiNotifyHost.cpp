// app/hmi/HmiNotifyHost.cpp - les notifications, a l'application (lot 14).
#include "HmiNotifyHost.hpp"

#include <chrono>
#include <ctime>
#include <utility>

namespace app {

void localDayMinute(int& weekday, int& minutes) {
    const std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    weekday = tm.tm_wday == 0 ? 7 : tm.tm_wday;
    minutes = tm.tm_hour * 60 + tm.tm_min;
}

double steadySeconds() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

NotifyHost::~NotifyHost() { shutdown(); }

void NotifyHost::shutdown() {
    if (dispatcher_) dispatcher_->stop();
    dispatcher_.reset();
    if (box_) box_->stop();
    box_.reset();
    configured_ = false;
}

void NotifyHost::tick(const hmi::Project* project) {
    const hmi::Notifications wanted = project ? project->notify : hmi::Notifications{};
    const std::string name = project ? project->config.name : std::string{};
    if (!dispatcher_) dispatcher_ = std::make_unique<hmi::notify::Dispatcher>();
    if (!configured_ || !(wanted == settings_) || name != project_) {
        settings_ = wanted;
        project_ = name;
        configured_ = true;
        dispatcher_->configure(settings_, project_.empty() ? std::string("IHM") : project_);
    }
    // La boite d'essai suit la configuration.
    const bool boxWanted = settings_.testBox;
    if (boxWanted && (!box_ || !box_->running() || boxPort_ != settings_.testPort)) {
        if (box_) box_->stop();
        box_ = std::make_unique<hmi::notify::TestServer>();
        boxPort_ = settings_.testPort;
        std::string why;
        if (box_->start(settings_.testPort, &why)) {
            boxError_.clear();
            events_.push_back("Bo\xC3\xAEte d'essai ouverte : SMTP 127.0.0.1:" + std::to_string(box_->smtpPort()) + ", SMS http://127.0.0.1:"
                              + std::to_string(box_->httpPort()));
        } else if (boxError_ != why) {
            boxError_ = why;
            events_.push_back("Bo\xC3\xAEte d'essai impossible : " + why);
        }
    } else if (!boxWanted && box_) {
        box_->stop();
        box_.reset();
        boxPort_ = 0;
        events_.push_back("Bo\xC3\xAEte d'essai ferm\xC3\xA9" "e");
    }
    dispatcher_->tick(steadySeconds());
    collect();
}

void NotifyHost::collect() {
    if (!dispatcher_) return;
    for (auto& s : dispatcher_->takeSent()) {
        std::string line;
        if (s.skipped) {
            line = (s.alarm.empty() ? std::string{} : s.alarm + " : ") + s.channel + (s.recipient.empty() ? std::string{} : " \xC3\xA0 " + s.recipient)
                 + " non envoy\xC3\xA9 - " + s.result;
        } else {
            line = s.channel + " \xC3\xA0 " + s.recipient + " (" + s.address + ")" + (s.alarm.empty() ? " [" + s.kind + "]" : " : " + s.alarm + " " + s.kind)
                 + (s.ok ? " - envoy\xC3\xA9" : " - \xC3\xA9" "chec : " + s.result);
        }
        events_.push_back(std::move(line));
        log_.push_back(std::move(s));
        while (log_.size() > 300) log_.pop_front();
    }
    // Personne ne les lit (aucune IHM en marche) : les 200 dernieres.
    if (events_.size() > 200) events_.erase(events_.begin(), events_.begin() + static_cast<std::ptrdiff_t>(events_.size() - 200));
}

void NotifyHost::notice(const hmi::AlarmNotice& n) {
    if (!dispatcher_) return;
    int weekday = 1, minutes = 0;
    localDayMinute(weekday, minutes);
    dispatcher_->notice(n, steadySeconds(), weekday, minutes);
    collect();
}

void NotifyHost::report(const hmi::ReportOutput& r) {
    if (!dispatcher_ || r.recipients.empty() || !r.data) return;
    const std::string type = r.format == "Excel" ? "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet" : "application/pdf";
    // Lot API 8 : Generer maintenant a pu l'ecrire ailleurs qu'en exports/ (le bouton ...).
    const std::string kept = r.path.empty() || r.path.rfind("exports/", 0) == 0 ? std::string("il est aussi dans le dossier exports/ du projet.")
                                                                                : "il est aussi enregistr\xC3\xA9 ici : " + r.path + ".";
    const std::string body = r.title + "\n\nDu " + r.from + " au " + r.to + ".\nLe rapport est en pi\xC3\xA8" "ce jointe (" + r.fileName
                           + ") ; " + kept + "\n\n-- \n" + (project_.empty() ? std::string("IHM") : project_)
                           + " (IHM XpgAnalyzer). Ce message part tout seul : ne pas y r\xC3\xA9pondre.";
    dispatcher_->sendReport(r.recipients, "[" + (project_.empty() ? std::string("IHM") : project_) + "] " + r.title, body, r.fileName, r.data, type);
    collect();
}

void NotifyHost::test(const hmi::Project* project, const hmi::NotifyRecipient& r, bool sms) {
    tick(project);
    dispatcher_->test(r, sms);
}

hmi::NotifyStats NotifyHost::stats() const { return dispatcher_ ? dispatcher_->stats() : hmi::NotifyStats{}; }

std::vector<std::string> NotifyHost::takeEvents() {
    collect();
    std::vector<std::string> out;
    out.swap(events_);
    return out;
}

bool NotifyHost::drain(int timeoutMs) {
    const bool ok = !dispatcher_ || dispatcher_->drain(timeoutMs);
    collect();
    return ok;
}

} // namespace app
