// =============================================================================
//  app/ai/AiAssistant.cpp - voir AiAssistant.hpp
// =============================================================================
#include "AiAssistant.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

namespace app::ai {

namespace {

std::string trim(std::string s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    return s.substr(i);
}

std::size_t countWords(std::string_view s) {
    std::size_t n = 0;
    bool in = false;
    for (const char c : s) {
        const bool blank = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!blank && !in) ++n;
        in = !blank;
    }
    return n;
}

// Les `n` premiers mots de `s`, avec ses blancs et ses retours a la ligne.
std::string firstWords(std::string_view s, std::size_t n) {
    std::size_t seen = 0;
    bool in = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        const bool blank = c == ' ' || c == '\n' || c == '\t' || c == '\r';
        if (!blank && !in) {
            if (seen == n) return std::string(s.substr(0, i));
            ++seen;
        }
        in = !blank;
    }
    return std::string(s);
}

const char* const kNewTitle = "Nouvelle conversation";

} // namespace

std::string_view phaseLabel(Assistant::Phase p) noexcept {
    switch (p) {
        case Assistant::Phase::Idle: return "";
        case Assistant::Phase::Waiting: return "En attente\xE2\x80\xA6";
        case Assistant::Phase::Thinking: return "R\xC3\xA9" "flexion\xE2\x80\xA6";
        case Assistant::Phase::Writing: return "\xC3\x89" "criture\xE2\x80\xA6";
    }
    return "";
}

Assistant::Assistant(Host host) : host_(std::move(host)) {}

// ---- le cycle de vie ------------------------------------------------------------------
void Assistant::load(std::vector<std::string>* problems) {
    bool found = false;
    settings_ = loadSettings(&found);
    conversations_ = loadConversations(problems);
    if (!found && conversations_.empty()) {
        // La premiere ouverture : les conversations de demonstration (marquees "demo"), et les reglages.
        conversations_ = demoConversations();
        for (auto& c : conversations_) (void)saveConversation(c);
        (void)saveSettings(settings_);
        appendActivity("conversation", "Premi\xC3\xA8re ouverture de l'Assistant : " + std::to_string(conversations_.size())
                                           + " conversations de d\xC3\xA9monstration.");
    }
    // La retention (Parametres > Historique) : les plus anciennes au-dela des limites.
    const auto expired = expiredConversations(conversations_, settings_);
    for (const auto& id : expired) {
        const int i = indexOf(id);
        if (i < 0) continue;
        (void)deleteConversationFiles(conversations_[static_cast<std::size_t>(i)]);
        appendActivity("nettoyage", "Conversation retir\xC3\xA9" "e (conservation de l'historique) : " + conversations_[static_cast<std::size_t>(i)].title);
        conversations_.erase(conversations_.begin() + i);
    }
    std::stable_sort(conversations_.begin(), conversations_.end(),
                     [](const Conversation& a, const Conversation& b) { return a.modified > b.modified; });
    current_ = conversations_.empty() ? -1 : 0;
    gen_ = {};
    pending_.clear();
    ++revision_;
}

bool Assistant::tick(double now) {
    lastNow_ = now;
    const std::uint64_t before = revision_;
    for (auto& p : pending_) {
        if (p.started < 0) p.started = now;
        const float prog = static_cast<float>(std::clamp((now - p.started) / kAttachSeconds, 0.0, 1.0));
        if (prog != p.progress) {
            p.progress = prog;
            ++revision_;
        }
    }
    if (gen_.phase != Phase::Idle) {
        if (gen_.start < 0) gen_.start = now;
        const double t = now - gen_.start;
        Phase ph = t < kWaiting ? Phase::Waiting : t < kWaiting + kThinking ? Phase::Thinking : Phase::Writing;
        if (ph != gen_.phase) {
            gen_.phase = ph;
            ++revision_;
        }
        if (ph == Phase::Writing) {
            const double w = (t - kWaiting - kThinking) * kWordsPerSecond;
            const auto shown = std::min<std::size_t>(gen_.words, static_cast<std::size_t>(std::max(0.0, std::floor(w))) + 1);
            if (shown != gen_.shownWords) {
                gen_.shownWords = shown;
                ++revision_;
            }
            if (gen_.shownWords >= gen_.words) finish();
        }
    }
    return revision_ != before;
}

// ---- les conversations ----------------------------------------------------------------
const Conversation* Assistant::current() const noexcept {
    return current_ >= 0 && current_ < static_cast<int>(conversations_.size()) ? &conversations_[static_cast<std::size_t>(current_)] : nullptr;
}
Conversation* Assistant::cur() noexcept {
    return current_ >= 0 && current_ < static_cast<int>(conversations_.size()) ? &conversations_[static_cast<std::size_t>(current_)] : nullptr;
}
Conversation* Assistant::generatingConversation() noexcept {
    if (gen_.phase == Phase::Idle) return nullptr;
    const int i = indexOf(gen_.conversation);
    return i < 0 ? nullptr : &conversations_[static_cast<std::size_t>(i)];
}
int Assistant::generatingMessage() const noexcept {
    if (gen_.phase == Phase::Idle) return -1;
    const auto* c = current();
    return c && c->id == gen_.conversation ? gen_.message : -1;
}

int Assistant::indexOf(std::string_view id) const noexcept {
    for (std::size_t i = 0; i < conversations_.size(); ++i)
        if (conversations_[i].id == id) return static_cast<int>(i);
    return -1;
}

void Assistant::newConversation() {
    // Celle qui est ouverte est deja vide : on y reste.
    if (const auto* c = current(); c && c->messages.empty()) {
        ++revision_;
        return;
    }
    Conversation c;
    c.id = newId("c");
    c.title = kNewTitle;
    c.created = isoDate(now());
    c.modified = c.created;
    conversations_.insert(conversations_.begin(), std::move(c));
    current_ = 0;   // une generation en cours suit sa conversation par son identifiant
    suggestionsAfter_ = false;
    ++revision_;
}

bool Assistant::select(std::string_view id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    if (i != current_) suggestionsAfter_ = false;
    current_ = i;
    ++revision_;
    return true;
}

bool Assistant::rename(std::string_view id, std::string title) {
    title = trim(std::move(title));
    const int i = indexOf(id);
    if (i < 0 || title.empty()) return false;
    auto& c = conversations_[static_cast<std::size_t>(i)];
    if (c.title == title) return true;
    c.title = std::move(title);
    save(c);
    ++revision_;
    return true;
}

bool Assistant::remove(std::string_view id) {
    const int i = indexOf(id);
    if (i < 0) return false;
    if (generating() && gen_.conversation == id) stop();
    const Conversation c = conversations_[static_cast<std::size_t>(i)];
    (void)deleteConversationFiles(c);
    appendActivity("suppression", "Conversation supprim\xC3\xA9" "e : " + c.title);
    conversations_.erase(conversations_.begin() + i);
    if (current_ == i) current_ = conversations_.empty() ? -1 : std::min(i, static_cast<int>(conversations_.size()) - 1);
    else if (current_ > i) --current_;
    suggestionsAfter_ = false;
    ++revision_;
    return true;
}

Conversation* Assistant::touch(Conversation& c) {
    c.modified = isoDate(now());
    // La plus recente en tete de la liste ; la conversation ouverte la suit.
    const std::string id = c.id;
    const std::string openId = current() ? current()->id : std::string{};
    std::stable_sort(conversations_.begin(), conversations_.end(),
                     [](const Conversation& a, const Conversation& b) { return a.modified > b.modified; });
    if (!openId.empty()) current_ = indexOf(openId);
    const int i = indexOf(id);
    return i < 0 ? nullptr : &conversations_[static_cast<std::size_t>(i)];
}

void Assistant::save(Conversation& c) {
    // Le message en cours d'ecriture n'est pas encore une reponse : il ne s'ecrit pas.
    if (generating() && gen_.conversation == c.id && gen_.message >= 0
        && gen_.message < static_cast<int>(c.messages.size())) {
        Conversation copy = c;
        copy.messages.erase(copy.messages.begin() + gen_.message);
        (void)saveConversation(copy);
        return;
    }
    (void)saveConversation(c);
}

// ---- envoyer, arreter ------------------------------------------------------------------
DemoContext Assistant::context() const {
    DemoContext ctx;
    ctx.env = host_.env;
    if (!host_.document) return ctx;
    const auto doc = host_.document();
    if (!doc) return ctx;
    ctx.project = &doc->project;
    const hmi::Id view = host_.activeView ? host_.activeView() : hmi::kNoId;
    ctx.view = view != hmi::kNoId ? doc->project.view(view) : nullptr;
    if (ctx.view && host_.selection) ctx.selection = host_.selection();
    return ctx;
}

bool Assistant::send(std::string text, std::string* why) {
    text = trim(std::move(text));
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!settings_.enabled) return fail("L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres de l'Assistant IA).");
    if (generating()) return fail("Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attendez-la, ou Arr\xC3\xAAtez-la.");
    if (processing()) return fail("Les pi\xC3\xA8" "ces jointes sont en cours de traitement.");
    if (text.empty() && pending_.empty()) return fail("Rien \xC3\xA0 envoyer.");
    if (!cur()) newConversation();
    Conversation& c = *cur();
    const auto doc = host_.document ? host_.document() : nullptr;   // tenu le temps de la demande
    DemoContext ctx = context();

    Message u;
    u.id = "m" + std::to_string(c.messages.size() + 1);
    u.role = "utilisateur";
    u.date = isoDate(now());
    u.text = text;
    for (auto& p : pending_) u.attachments.push_back(p.attachment);
    pending_.clear();
    ctx.attachments = u.attachments;
    const bool first = c.messages.empty();
    if (first || c.title == kNewTitle) c.title = text.empty() ? std::string("Pi\xC3\xA8" "ces jointes") : titleFor(text);
    if (first) {
        c.project.name = host_.projectName ? host_.projectName() : std::string{};
        c.project.path = host_.projectPath ? host_.projectPath() : std::string{};
    }
    if (ctx.view) c.project.view = ctx.view->name;
    c.messages.push_back(std::move(u));

    DemoReply reply = demoReply(text, ctx);
    Message a;
    a.id = "m" + std::to_string(c.messages.size() + 1);
    a.role = "assistant";
    a.date = isoDate(now());
    c.messages.push_back(std::move(a));

    gen_ = {};
    gen_.phase = Phase::Waiting;
    gen_.start = -1;
    gen_.message = static_cast<int>(c.messages.size()) - 1;
    gen_.conversation = c.id;
    gen_.full = std::move(reply.text);
    gen_.words = countWords(gen_.full);
    gen_.proposal = std::move(reply.proposal);
    gen_.suggestions = reply.suggestions;
    suggestionsAfter_ = false;
    (void)touch(c);
    if (first) appendActivity("conversation", "Nouvelle conversation : " + cur()->title);
    for (const auto& att : cur()->messages[static_cast<std::size_t>(gen_.message) - 1].attachments)
        appendActivity("fichier", "Pi\xC3\xA8" "ce jointe : " + att.name + " (" + formatBytes(att.bytes) + ")");
    save(*cur());
    ++revision_;
    (void)doc;
    return true;
}

std::string Assistant::shownText() const {
    if (gen_.phase != Phase::Writing) return {};
    return firstWords(gen_.full, gen_.shownWords);
}

void Assistant::finish() {
    Conversation* c = generatingConversation();
    if (!c || gen_.message < 0 || gen_.message >= static_cast<int>(c->messages.size())) {
        gen_ = {};
        ++revision_;
        return;
    }
    Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
    m.text = gen_.full;
    m.state = "fini";
    m.date = isoDate(now());
    const int index = gen_.message;
    if (gen_.proposal) {
        Proposal p = std::move(*gen_.proposal);
        p.state = "proposee";
        if (const auto doc = host_.document ? host_.document() : nullptr) {
            const Check ck = validate(doc->project, p, host_.env);
            p.create = ck.create;
            p.modify = ck.modify;
            p.remove = ck.remove;
        }
        m.proposal = std::move(p);
    }
    const bool open = current() && current()->id == c->id;
    suggestionsAfter_ = open && gen_.suggestions;
    const bool hasProposal = m.proposal.has_value();
    gen_ = {};
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    // Le mode Direct applique sur la conversation ouverte (l'autre attend qu'on y revienne).
    if (hasProposal && open && directMode()) autoApply(index);
}

void Assistant::stop() {
    if (!generating()) return;
    Conversation* c = generatingConversation();
    if (c && gen_.message >= 0 && gen_.message < static_cast<int>(c->messages.size())) {
        Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
        const std::string shown = shownText();
        m.text = shown.empty() ? std::string("*(Arr\xC3\xAAt\xC3\xA9" "e avant la r\xC3\xA9ponse.)*") : shown + " \xE2\x80\xA6";
        m.state = "arrete";
        m.date = isoDate(now());
        gen_ = {};
        save(*c);
    } else {
        gen_ = {};
    }
    notice_ = "G\xC3\xA9n\xC3\xA9ration arr\xC3\xAAt\xC3\xA9" "e : la r\xC3\xA9ponse est gard\xC3\xA9" "e telle quelle, sans proposition.";
    ++revision_;
}

// ---- les pieces jointes -------------------------------------------------------------------
bool Assistant::attach(const std::string& path, std::string* why) {
    if (generating()) {
        if (why) *why = "Attendez la fin de la r\xC3\xA9ponse pour joindre un fichier.";
        return false;
    }
    Attachment a;
    if (!addAttachment(path, a, why)) return false;
    Pending p;
    p.attachment = std::move(a);
    if (lastNow_ > 0) p.started = lastNow_;   // le traitement part de maintenant (sinon : au prochain tick)
    pending_.push_back(std::move(p));
    ++revision_;
    return true;
}

void Assistant::detach(std::size_t index) {
    if (index >= pending_.size()) return;
    // La copie partie avec : elle n'appartient a aucun message.
    Conversation tmp;
    Message m;
    m.attachments.push_back(pending_[index].attachment);
    tmp.messages.push_back(std::move(m));
    (void)deleteConversationFiles(tmp);
    pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(index));
    ++revision_;
}

bool Assistant::processing() const noexcept {
    return std::any_of(pending_.begin(), pending_.end(), [](const Pending& p) { return p.progress < 1.f; });
}

// ---- les propositions ----------------------------------------------------------------------
const Proposal* Assistant::proposal(int message) const noexcept {
    const auto* c = current();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size())) return nullptr;
    const auto& m = c->messages[static_cast<std::size_t>(message)];
    return m.proposal ? &*m.proposal : nullptr;
}

Check Assistant::check(int message) const {
    Check ck;
    const Proposal* p = proposal(message);
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!p || !doc) {
        ck.errors.push_back(doc ? "Pas de proposition." : "Pas de projet IHM ouvert : rien ne peut s'appliquer.");
        ck.viewMissing = !doc;
        return ck;
    }
    return validate(doc->project, *p, host_.env);
}

Preview Assistant::previewOf(int message) const {
    const Proposal* p = proposal(message);
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!p || !doc) {
        Preview pv;
        pv.why = "Pas de projet IHM ouvert.";
        return pv;
    }
    return preview(doc->project, *p);
}

bool Assistant::needsConfirm(int message) const { return check(message).confirmRemoval; }

bool Assistant::apply(int message, std::string* why) {
    auto* c = cur();
    const Proposal* p = proposal(message);
    if (!c || !p) {
        if (why) *why = "Pas de proposition.";
        return false;
    }
    if (p->state != "proposee") {
        if (why) *why = p->state == "appliquee" ? "Cette proposition est d\xC3\xA9j\xC3\xA0 appliqu\xC3\xA9" "e." : "Cette proposition a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e.";
        return false;
    }
    const auto doc = host_.document ? host_.document() : nullptr;
    std::string w;
    auto cmd = makeCommand(doc, *p, &w);
    if (!cmd) {
        if (why) *why = w;
        notice_ = w;
        ++revision_;
        return false;
    }
    if (host_.apply) host_.apply(std::move(cmd));
    auto& prop = *c->messages[static_cast<std::size_t>(message)].proposal;
    prop.state = "appliquee";
    notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e (Ctrl+Z l'annule d'un coup).";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::refuse(int message) {
    auto* c = cur();
    const Proposal* p = proposal(message);
    if (!c || !p || p->state != "proposee") return false;
    c->messages[static_cast<std::size_t>(message)].proposal->state = "refusee";
    notice_ = "Proposition refus\xC3\xA9" "e : le projet n'a pas chang\xC3\xA9.";
    save(*c);
    ++revision_;
    return true;
}

bool Assistant::chooseVariable(int message, VariableChoice choice) {
    auto* c = cur();
    const Proposal* p = proposal(message);
    if (!c || !p || p->state != "proposee") return false;
    c->messages[static_cast<std::size_t>(message)].proposal->variable = std::move(choice);
    save(*c);
    ++revision_;
    return true;
}

void Assistant::autoApply(int message) {
    if (needsConfirm(message)) {
        directConfirm_ = message;   // l'ecran demande d'abord (suppression multiple)
        return;
    }
    std::string why;
    if (!apply(message, &why)) notice_ = "Mode Direct : " + why;
}

// ---- le mode, les reglages -------------------------------------------------------------------
void Assistant::setDirectMode(bool direct) {
    const std::string mode = direct ? "direct" : "previsualisation";
    if (settings_.mode == mode) return;
    settings_.mode = mode;
    (void)saveSettings(settings_);
    ++revision_;
}

void Assistant::setSettings(Settings s) {
    settings_ = std::move(s);
    (void)saveSettings(settings_);
    ++revision_;
}

std::string Assistant::connectionText() const {
    if (!settings_.enabled) return "Assistant d\xC3\xA9sactiv\xC3\xA9";
    if (settings_.provider == "demo") return "D\xC3\xA9monstration \xE2\x80\x93 aucune IA connect\xC3\xA9" "e";
    return "Fournisseur pas encore branch\xC3\xA9 (d\xC3\xA9monstration)";
}

} // namespace app::ai
