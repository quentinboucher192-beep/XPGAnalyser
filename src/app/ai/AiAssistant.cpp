// =============================================================================
//  app/ai/AiAssistant.cpp - voir AiAssistant.hpp
// =============================================================================
#include "AiAssistant.hpp"

#include "AiHttp.hpp"
#include "AiLocal.hpp"
#include "AiOpenAI.hpp"
#include "AiPrompt.hpp"
#include "AiSecret.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <tuple>

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
// 1.16.0 : l'IA integree repond vite (des regles sur ce PC) : une attente et une reflexion courtes.
constexpr double kLocalWaiting = 0.15, kLocalThinking = 0.45;
const char* const kStoppedEmpty = "*(Arr\xC3\xAAt\xC3\xA9" "e avant la r\xC3\xA9ponse.)*";

// Le libelle d'un fournisseur ("Anthropic (Claude)" -> "Anthropic").
std::string providerName(std::string_view key) {
    for (const auto& p : providers())
        if (p.key == key) {
            std::string l(p.label);
            if (const auto par = l.find(" ("); par != std::string::npos) l.resize(par);
            return l;
        }
    return std::string(key);
}

std::string plural(std::size_t n, const char* one, const char* many) { return std::to_string(n) + " " + (n > 1 ? many : one); }

} // namespace

std::string_view phaseLabel(Assistant::Phase p) noexcept {
    switch (p) {
        case Assistant::Phase::Idle: return "";
        case Assistant::Phase::Waiting: return "En attente\xE2\x80\xA6";
        case Assistant::Phase::Thinking: return "R\xC3\xA9" "flexion\xE2\x80\xA6";
        case Assistant::Phase::Writing: return "\xC3\x89" "criture\xE2\x80\xA6";
        case Assistant::Phase::Proposing: return "Pr\xC3\xA9paration des op\xC3\xA9rations\xE2\x80\xA6";
    }
    return "";
}

Assistant::Assistant(Host host) : host_(std::move(host)) {}

// Le fournisseur en cours est arrete et part finir sur son fil (son destructeur attend le fil :
// jamais sur celui de l'interface tant qu'il travaille - tick le libere quand il a fini).
Assistant::~Assistant() = default;

// ---- le cycle de vie ------------------------------------------------------------------
void Assistant::load(std::vector<std::string>* problems) {
    bool found = false;
    settings_ = loadSettings(&found);
    // 1.15.0 : la cle memorisee (protegee par Windows) revient pour la seance.
    if (apiKey_.empty() && hasStoredKey()) {
        std::string why;
        if (auto key = loadStoredKey(&why)) apiKey_ = std::move(*key);
        else if (!why.empty()) notice_ = "Assistant IA : " + why;
    }
    conversations_ = loadConversations(problems);
    if (!found && conversations_.empty()) {
        // La premiere ouverture : les conversations de demonstration (marquees "demo"), et les reglages.
        conversations_ = demoConversations(settings_.provider);
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
    retire();
    gen_ = {};
    pending_.clear();
    totalsDay_.clear();   // 1.16.0 : les cumuls relus
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
    // 1.15.0 : les fournisseurs arretes qui ont fini leur fil ; la liste des modeles.
    retiring_.erase(std::remove_if(retiring_.begin(), retiring_.end(), [](const std::unique_ptr<Provider>& p) { return !p || !p->busy(); }),
                    retiring_.end());
    (void)pollModels();
    if (gen_.phase != Phase::Idle && gen_.live) {
        pollLive();
    } else if (gen_.phase != Phase::Idle) {
        if (gen_.start < 0) gen_.start = now;
        const double t = now - gen_.start;
        const double waiting = gen_.local ? kLocalWaiting : kWaiting, thinking = gen_.local ? kLocalThinking : kThinking;
        Phase ph = t < waiting ? Phase::Waiting : t < waiting + thinking ? Phase::Thinking : Phase::Writing;
        if (ph != gen_.phase) {
            gen_.phase = ph;
            ++revision_;
        }
        if (ph == Phase::Writing) {
            const double w = (t - waiting - thinking) * kWordsPerSecond;
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
    if (generating()) return fail("Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attends-la, ou arr\xC3\xAAte-la.");
    if (processing()) return fail("Les pi\xC3\xA8" "ces jointes sont en cours de traitement.");
    if (text.empty() && pending_.empty()) return fail("Rien \xC3\xA0 envoyer.");
    if (live()) {
        // 1.15.0 : un vrai fournisseur. Rien ne change dans la conversation si l'envoi ne part pas.
        std::string w;
        if (!ready(&w)) return fail(w);
        // 1.16.0 : le budget atteint - l'envoi demande confirmation (le panneau la pose ; rien n'est
        // bloque sans le dire).
        if (budgetBlocks(&w)) return fail(w);
        overBudgetOk_ = false;
        if (!cur()) newConversation();
        Conversation& c = *cur();
        const Conversation before = c;
        Message u;
        u.id = "m" + std::to_string(c.messages.size() + 1);
        u.role = "utilisateur";
        u.date = isoDate(now());
        u.text = text;
        for (const auto& p : pending_) u.attachments.push_back(p.attachment);
        const bool first = c.messages.empty();
        if (first || c.title == kNewTitle) c.title = text.empty() ? std::string("Pi\xC3\xA8" "ces jointes") : titleFor(text);
        if (first) {
            c.project.name = host_.projectName ? host_.projectName() : std::string{};
            c.project.path = host_.projectPath ? host_.projectPath() : std::string{};
        }
        if (const DemoContext ctx = context(); ctx.view) c.project.view = ctx.view->name;
        c.messages.push_back(std::move(u));
        if (!startLive(c, text, &w)) {
            c = before;
            return fail(w);
        }
        // Les pieces qui ne partent pas entieres (un classeur, une image trop lourde) : dites.
        std::string notes;
        if (settings_.sendAttachments)
            for (const auto& p : pending_) {
                ProviderAttachment pa{p.attachment.name, mediaTypeOf(p.attachment.name), attachmentPath(p.attachment), p.attachment.bytes};
                if (const auto plan = planAttachment(pa); !plan.note.empty()) notes += (notes.empty() ? "" : " ") + plan.note;
            }
        pending_.clear();
        suggestionsAfter_ = false;
        Conversation* cc = touch(c);
        if (cc) {
            if (first) appendActivity("conversation", "Nouvelle conversation : " + cc->title);
            for (const auto& att : cc->messages[static_cast<std::size_t>(gen_.message) - 1].attachments)
                appendActivity("fichier", "Pi\xC3\xA8" "ce jointe : " + att.name + " (" + formatBytes(att.bytes) + ")");
            save(*cc);
        }
        if (!notes.empty()) notice_ = "Assistant IA : " + notes;
        ++revision_;
        return true;
    }
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

    // 1.16.0 : l'IA integree (sur ce PC, des regles) ; sinon la demonstration.
    DemoReply reply;
    localSuggestions_.clear();
    if (local()) {
        PromptInput in;
        in.project = ctx.project;
        in.view = ctx.view;
        in.selection = ctx.selection;
        in.settings = settings_;
        in.env = host_.env;
        in.projectName = host_.projectName ? host_.projectName() : std::string{};
        in.request = text;
        // les tours d'avant (les six derniers) : la demande, la proposition de sa reponse
        std::vector<LocalTurn> history;
        for (std::size_t i = 0; i + 1 < c.messages.size(); ++i) {
            if (c.messages[i].role != "utilisateur") continue;
            LocalTurn turn;
            turn.request = c.messages[i].text;
            if (i + 1 < c.messages.size() - 1 && c.messages[i + 1].role == "assistant") turn.proposal = c.messages[i + 1].proposal;
            history.push_back(std::move(turn));
        }
        if (history.size() > 6) history.erase(history.begin(), history.end() - 6);
        LocalReply lr = understand(in, text, history);
        reply.text = std::move(lr.text);
        reply.proposal = std::move(lr.proposal);
        // 1.16.0 (captures) : l'empreinte d'avant, celle de ce que la proposition touche (la vue ouverte, ou la vue
        // neuve encore absente, les variables et types crees) - la carte saura dire qu'un Ctrl+Z l'a reprise.
        if (reply.proposal && ctx.project) {
            if (createsView(*reply.proposal)) {   // la vue neuve : ni le nom ni l'identifiant de la vue ouverte
                reply.proposal->view = newViewName(*reply.proposal);
                reply.proposal->viewId = hmi::kNoId;
            }
            reply.proposal->fingerprint = proposalPrint(*ctx.project, *reply.proposal, ctx.view ? fingerprint(*ctx.view) : std::string{});
        }
        reply.suggestions = !lr.suggestions.empty();
        localSuggestions_ = std::move(lr.suggestions);
        if (!ctx.attachments.empty()) {
            std::string files;
            for (const auto& att : ctx.attachments) files += (files.empty() ? "" : ", ") + std::string("**") + att.name + "**";
            reply.text = "J'ai bien re\xC3\xA7u " + files + ", mais l'IA int\xC3\xA9gr\xC3\xA9" "e ne lit pas le contenu des pi\xC3\xA8" "ces jointes.\n\n"
                         + reply.text;
        }
    } else {
        reply = demoReply(text, ctx);
    }
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
    gen_.local = local();
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
    if (gen_.live && gen_.textTool) {
        // 1.16.0 : la proposition en texte (un bloc ```json) ne s'ecrit pas dans la bulle : la carte la montre.
        const auto at = gen_.text.find("```");
        return at == std::string::npos ? gen_.text : gen_.text.substr(0, at);
    }
    if (gen_.live) return gen_.text;   // un vrai fournisseur : ce qui est arrive
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
    // Un vrai fournisseur : la connexion se ferme ; il finit sur son fil (retiring_), sans bloquer.
    if (gen_.live) retire();
    Conversation* c = generatingConversation();
    if (c && gen_.message >= 0 && gen_.message < static_cast<int>(c->messages.size())) {
        Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
        const std::string shown = shownText();
        m.text = shown.empty() ? std::string(kStoppedEmpty) : shown + " \xE2\x80\xA6";
        m.state = "arrete";
        m.date = isoDate(now());
        m.provider = gen_.provider;
        m.model = gen_.model;
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
        if (why) *why = "Attends la fin de la r\xC3\xA9ponse pour joindre un fichier.";
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
    return preview(doc->project, *p, host_.env);
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
    auto cmd = makeCommand(doc, *p, &w, host_.env);
    if (!cmd) {
        if (why) *why = w;
        notice_ = w;
        ++revision_;
        return false;
    }
    if (host_.apply) host_.apply(std::move(cmd));
    auto& prop = *c->messages[static_cast<std::size_t>(message)].proposal;
    prop.state = "appliquee";
    // L'empreinte d'apres : la carte saura dire si un Ctrl+Z l'a reprise. 1.16.0 : proposalPrint (la vue
    // neuve, les variables et les types crees comptent) ; la vue neuve retient son identifiant.
    prop.fingerprintAfter.clear();
    hmi::Id opened = hmi::kNoId;
    if (const auto after = host_.document ? host_.document() : nullptr) {
        if (createsView(prop))
            if (const auto* nv = after->project.viewByName(newViewName(prop))) prop.viewId = opened = nv->id;
        prop.fingerprintAfter = proposalPrint(after->project, prop);
    }
    notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e (Ctrl+Z l'annule d'un coup).";
    if (const std::string made = creationSummary(prop); !made.empty())
        notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e - cr\xC3\xA9\xC3\xA9" "s : " + made + " (Ctrl+Z reprend tout d'un coup).";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    // 1.16.0 : la vue creee s'ouvre dans l'editeur.
    if (opened != hmi::kNoId && host_.openView) host_.openView(opened);
    return true;
}

Assistant::Applied Assistant::appliedState(int message) const {
    const Proposal* p = proposal(message);
    return p ? appliedOf(*p) : Applied::NotApplied;
}

Assistant::Applied Assistant::appliedOf(const Proposal& prop) const {
    const Proposal* p = &prop;
    if (p->state != "appliquee") return Applied::NotApplied;
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return Applied::Applied;   // pas de projet ouvert : rien a comparer
    // 1.16.0 : l'empreinte de ce que la proposition touche (proposalPrint) - la vue visee comme avant ; la vue
    // neuve, les variables et les types crees (Ctrl+Z les retire : annulee).
    if (usesOpenView(*p) && !targetView(doc->project, *p)) return Applied::Changed;
    const std::string now = proposalPrint(doc->project, *p);
    if (now == p->fingerprintAfter) return Applied::Applied;
    if (now == p->fingerprint) return Applied::Undone;
    // Un fichier d'avant 1.14.2 n'a pas l'empreinte d'apres : on ne sait pas mieux qu'"appliquee".
    return p->fingerprintAfter.empty() ? Applied::Applied : Applied::Changed;
}

bool Assistant::reapply(int message, std::string* why) {
    auto* c = cur();
    if (!c || appliedState(message) != Applied::Undone) {
        if (why) *why = "Rien \xC3\xA0 r\xC3\xA9" "appliquer : la proposition n'est pas annul\xC3\xA9" "e.";
        return false;
    }
    c->messages[static_cast<std::size_t>(message)].proposal->state = "proposee";
    if (apply(message, why)) return true;
    if (auto* again = cur(); again && message < static_cast<int>(again->messages.size()) && again->messages[static_cast<std::size_t>(message)].proposal)
        again->messages[static_cast<std::size_t>(message)].proposal->state = "appliquee";
    ++revision_;
    return false;
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
    // 1.16.0 : un autre service (fournisseur, adresse) - la limite lue sur l'ancien ne vaut plus.
    if (s.provider != settings_.provider || trim(s.endpoint) != trim(settings_.endpoint) || s.preset != settings_.preset) {
        const long long conversation = limit_.conversation;
        limit_ = LimitState{};
        limit_.conversation = conversation;
    }
    settings_ = std::move(s);
    (void)saveSettings(settings_);
    ++revision_;
}

std::string Assistant::connectionText() const {
    if (!settings_.enabled) return "Assistant d\xC3\xA9sactiv\xC3\xA9";
    if (local()) return "IA int\xC3\xA9gr\xC3\xA9" "e \xE2\x80\x93 sans connexion";
    if (!live()) return "D\xC3\xA9monstration \xE2\x80\x93 aucune IA connect\xC3\xA9" "e";
    std::string why;
    if (!providerFactory_ && !httpAvailable(&why)) {
        if (!why.empty()) why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
        return why;
    }
    if (needsSetup()) return "\xC3\x80 configurer : choisis un mod\xC3\xA8le et une cl\xC3\xA9";
    // 1.16.0 : un service compatible OpenAI se nomme par son service ("Ollama - llama3.2").
    if (settings_.provider == "openai") {
        std::string name = "Compatible OpenAI";
        if (const auto* sp = findServicePreset(settings_.preset); sp && sp->key != "autre") {
            name = std::string(sp->label);
            if (const auto par = name.find(" ("); par != std::string::npos) name.resize(par);
        }
        return name + " \xE2\x80\x93 " + settings_.model;
    }
    return providerName(settings_.provider) + " \xE2\x80\x93 " + settings_.model + (settings_.endpoint.empty() ? std::string{} : std::string(" (relais)"));
}

// 1.16.0 : les suggestions de l'IA integree, ou celles de la demonstration.
const std::vector<std::string>& Assistant::suggestions() const {
    if (!local()) return demoSuggestions();
    return suggestionsAfter_ && !localSuggestions_.empty() ? localSuggestions_ : localSuggestions();
}

// ---- la cle ------------------------------------------------------------------------------------
bool Assistant::rememberKey(bool on, std::string* why) {
    ++revision_;
    if (!on) return forgetStoredKey();
    if (apiKey_.empty()) {
        if (why) *why = "Pas de cl\xC3\xA9 \xC3\xA0 m\xC3\xA9moriser.";
        return false;
    }
    return storeKey(apiKey_, why);
}

bool Assistant::keyRemembered() const { return hasStoredKey(); }

// ---- le fournisseur (1.15.0) ----------------------------------------------------------------------
bool Assistant::needsSetup() const {
    if (!live()) return false;
    // 1.16.0 : la cle selon le fournisseur (providerNeedsKey : un relais, un modele sur ce PC n'en ont pas besoin).
    return trim(settings_.model).empty() || (apiKey_.empty() && providerNeedsKey(settings_));
}

bool Assistant::ready(std::string* why) const {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!live()) return true;
    bool available = providerFactory_ != nullptr;
    for (const auto& p : providers())
        if (p.key == settings_.provider) available = available || p.available;
    if (!available) return fail("Ce fournisseur n'est pas encore branch\xC3\xA9 : choisis Anthropic (Claude), ou la D\xC3\xA9monstration (Param\xC3\xA8tres).");
    if (trim(settings_.model).empty())
        return fail("\xC3\x80 configurer : choisis un mod\xC3\xA8le dans les Param\xC3\xA8tres de l'Assistant IA (\xC2\xAB Lister les mod\xC3\xA8les \xC2\xBB).");
    if (apiKey_.empty() && providerNeedsKey(settings_))
        return fail("\xC3\x80 configurer : entre ta cl\xC3\xA9 API dans les Param\xC3\xA8tres de l'Assistant IA.");
    std::string w;
    if (!providerFactory_ && !httpAvailable(&w)) return fail("Pas de connexion possible sur ce poste : " + w + ".");
    return true;
}

void Assistant::retire() {
    if (!provider_) return;
    provider_->cancel();
    if (provider_->busy()) retiring_.push_back(std::move(provider_));
    provider_.reset();
}

std::pair<std::string, bool> Assistant::toolResult(const Message& m) const {
    if (!m.tool) return {};
    if (!m.tool->error.empty())
        return {"Erreur : rien n'a \xC3\xA9t\xC3\xA9 propos\xC3\xA9 \xC3\xA0 l'utilisateur, l'entr\xC3\xA9" "e de l'outil est invalide : " + m.tool->error, true};
    if (!m.proposal) return {"Erreur : rien n'a \xC3\xA9t\xC3\xA9 propos\xC3\xA9 \xC3\xA0 l'utilisateur.", true};
    const Proposal& p = *m.proposal;
    if (p.state == "refusee") return {"refus\xC3\xA9" "e par l'utilisateur : le projet n'a pas chang\xC3\xA9.", false};
    if (p.state == "appliquee") {
        switch (appliedOf(p)) {
            case Applied::Undone: return {"annul\xC3\xA9" "e (Ctrl+Z) : appliqu\xC3\xA9" "e puis annul\xC3\xA9" "e par l'utilisateur, la vue est revenue \xC3\xA0 son \xC3\xA9tat d'avant.", false};
            case Applied::Changed: return {"appliqu\xC3\xA9" "e ; la vue a chang\xC3\xA9 depuis (d'autres modifications de l'utilisateur).", false};
            default: return {"appliqu\xC3\xA9" "e au projet.", false};
        }
    }
    // Encore proposee : la validation d'aujourd'hui dit si elle peut s'appliquer.
    if (const auto doc = host_.document ? host_.document() : nullptr) {
        const Check ck = validate(doc->project, p, host_.env);
        // Refusee par la validation : le texte ecrit pour le modele (les erreurs, le detail des
        // genres en cause) - il corrige au tour suivant.
        if (!ck.ok()) return {feedback(ck, p), true};
        if (ck.stale) return {"en attente : pas encore appliqu\xC3\xA9" "e, et la vue a chang\xC3\xA9 depuis (elle ne s'appliquera plus telle quelle).", false};
    }
    return {"en attente : pas encore appliqu\xC3\xA9" "e (l'utilisateur ne l'a ni appliqu\xC3\xA9" "e ni refus\xC3\xA9" "e).", false};
}

ProviderRequest Assistant::requestFor(const Conversation& c, std::size_t upTo) const {
    ProviderRequest r;
    r.model = trim(settings_.model);
    r.endpoint = trim(settings_.endpoint);
    r.timeoutSeconds = settings_.timeoutSeconds;
    r.maxTokens = 16000;
    r.effort = settings_.effort;
    std::string pendingId, pendingText;
    bool pendingError = false;
    for (std::size_t i = 0; i < c.messages.size() && i < upTo; ++i) {
        const Message& m = c.messages[i];
        ProviderTurn t;
        if (m.role == "utilisateur") {
            t.role = ProviderTurn::Role::User;
            t.text = m.text;
            if (settings_.sendAttachments) {
                for (const auto& a : m.attachments) t.attachments.push_back({a.name, mediaTypeOf(a.name), attachmentPath(a), a.bytes});
            } else if (!m.attachments.empty()) {
                t.text += "\n\n[" + plural(m.attachments.size(), "pi\xC3\xA8" "ce jointe", "pi\xC3\xA8" "ces jointes")
                          + " : pas envoy\xC3\xA9" "e(s), les r\xC3\xA9glages de confidentialit\xC3\xA9 les gardent sur ce poste.]";
            }
            if (!pendingId.empty()) {
                t.toolResultFor = pendingId;
                t.toolResultText = pendingText;
                t.toolResultError = pendingError;
                pendingId.clear();
            }
        } else {
            // Une erreur du logiciel n'est pas un tour du modele ; un arret sans texte non plus.
            if (m.state == "erreur") continue;
            if (m.state == "arrete" && m.text == kStoppedEmpty) continue;
            t.role = ProviderTurn::Role::Assistant;
            t.text = m.text;
            if (m.tool) {
                t.toolUseId = m.tool->id;
                t.toolName = m.tool->name;
                t.toolInputJson = m.tool->input;
                pendingId = m.tool->id;
                std::tie(pendingText, pendingError) = toolResult(m);
            } else {
                pendingId.clear();
            }
        }
        r.turns.push_back(std::move(t));
    }
    return r;
}

bool Assistant::startLive(Conversation& c, std::string request, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    auto prov = providerFactory_ ? providerFactory_(settings_.provider) : makeProvider(settings_.provider);
    if (!prov) return fail("Ce fournisseur n'est pas encore branch\xC3\xA9.");
    const auto doc = host_.document ? host_.document() : nullptr;   // tenu le temps de la preparation
    const DemoContext ctx = context();
    PromptInput in;
    in.project = ctx.project;
    in.view = ctx.view;
    in.selection = ctx.selection;
    in.settings = settings_;
    in.env = host_.env;
    in.projectName = host_.projectName ? host_.projectName() : std::string{};
    in.request = request;   // les genres que la demande evoque ont le detail de leurs proprietes
    ProviderRequest req = requestFor(c, c.messages.size());
    req.apiKey = apiKey_;
    // 1.16.0 : le service, la fenetre de contexte, l'outil (Auto : en texte si le modele le refuse).
    req.preset = settings_.preset;
    req.contextTokens = contextWindow();
    req.toolsInText = toolsInText();
    req.textFallback = settings_.toolMode != "outil";
    // Sur le fil de l'interface (le registre se construit une fois) : les consignes en deux morceaux ;
    // une petite fenetre de contexte (un petit modele gratuit) : resserrees.
    PromptParts parts = req.contextTokens > 0 && req.contextTokens < kSmallContext ? compactPromptParts(in, req.contextTokens) : promptParts(in);
    req.system = parts.stable + parts.dynamic;
    req.systemStable = std::move(parts.stable);
    req.systemDynamic = std::move(parts.dynamic);
    req.toolsJson = operationsTool();
    if (!prov->start(std::move(req), why)) return false;
    retire();
    provider_ = std::move(prov);
    Message a;
    a.id = "m" + std::to_string(c.messages.size() + 1);
    a.role = "assistant";
    a.date = isoDate(now());
    c.messages.push_back(std::move(a));
    gen_ = {};
    gen_.live = true;
    gen_.phase = Phase::Waiting;
    gen_.message = static_cast<int>(c.messages.size()) - 1;
    gen_.conversation = c.id;
    gen_.request = std::move(request);
    gen_.provider = settings_.provider;
    gen_.model = trim(settings_.model);
    gen_.textTool = toolsInText();
    if (ctx.view) {
        gen_.viewId = ctx.view->id;
        gen_.fingerprint = fingerprint(*ctx.view);
    }
    (void)doc;
    ++revision_;
    return true;
}

void Assistant::pollLive() {
    if (!provider_) {
        ProviderEvent e;
        e.type = ProviderEvent::Type::Error;
        e.text = "Le fournisseur s'est arr\xC3\xAAt\xC3\xA9 sans r\xC3\xA9ponse.";
        e.retryable = true;
        failLive(e);
        return;
    }
    for (auto& e : provider_->poll()) {
        switch (e.type) {
            case ProviderEvent::Type::Status: {
                Phase ph = gen_.phase;
                if (e.text == "connexion") ph = Phase::Waiting;
                else if (e.text == "reprise") ph = Phase::Waiting;
                else if (e.text == "reflexion") ph = Phase::Thinking;
                else if (e.text == "ecriture") ph = Phase::Writing;
                else if (e.text == "operations") ph = Phase::Proposing;
                // 1.16.0 : le modele refuse l'appel d'outil - la proposition en texte, maintenant et ensuite.
                if (e.text == "sans-outil") {
                    noTools_ = settings_.provider + "|" + trim(settings_.endpoint) + "|" + gen_.model;
                    gen_.textTool = true;
                    notice_ = "Assistant IA : " + e.detail;
                    ph = Phase::Waiting;
                }
                const std::string detail = e.text == "reprise" || e.text == "sans-outil" ? e.detail : e.text == "connexion" ? gen_.detail : std::string{};
                if (detail != gen_.detail) {
                    gen_.detail = detail;
                    ++revision_;
                }
                if (ph != gen_.phase) {
                    gen_.phase = ph;
                    ++revision_;
                }
                break;
            }
            case ProviderEvent::Type::Text:
                gen_.text += e.text;
                if (gen_.phase != Phase::Proposing) gen_.phase = Phase::Writing;
                // 1.16.0 : le bloc de la proposition en texte commence - "Preparation des operations".
                if (gen_.textTool && gen_.text.find("```") != std::string::npos) gen_.phase = Phase::Proposing;
                gen_.detail.clear();
                ++revision_;
                break;
            case ProviderEvent::Type::Usage:
                // 1.16.0 : la limite du service (les en-tetes) et les jetons de la reponse (le dernier fait foi).
                if (e.limits.known() || e.limits.retryAfterSeconds >= 0) {
                    limit_.limits = e.limits;
                    limit_.at = lastNow_;
                    limit_.clock = now();
                    limit_.provider = gen_.provider;
                }
                if (!e.usage.empty()) gen_.usage = e.usage;
                ++revision_;
                break;
            case ProviderEvent::Type::ToolUse:
                gen_.tool = ToolCall{e.toolUseId, e.toolName, e.toolInputJson, {}};
                ++revision_;
                break;
            case ProviderEvent::Type::Done:
                finishLive(e.stopReason);
                return;
            case ProviderEvent::Type::Error:
                failLive(e);
                return;
        }
    }
}

void Assistant::finishLive(const std::string& stopReason) {
    retire();
    recordLive();
    Conversation* c = generatingConversation();
    if (!c || gen_.message < 0 || gen_.message >= static_cast<int>(c->messages.size())) {
        gen_ = {};
        ++revision_;
        return;
    }
    if (stopReason == "arrete") {   // arrete ailleurs (le fournisseur a ete coupe) : comme Arreter
        stop();
        return;
    }
    Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
    m.text = trim(gen_.text);
    m.state = "fini";
    m.date = isoDate(now());
    m.provider = gen_.provider;
    m.model = gen_.model;
    // 1.16.0 : les jetons de la reponse (le cumul de la conversation) ; la proposition en texte : le
    // bloc ```json sort de la bulle (la carte la montre ; le tour suivant la reecrit pour le modele).
    if (!gen_.usage.empty()) m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total()));
    if (gen_.textTool && gen_.tool) {
        std::string rest;
        if (extractProposalBlock(m.text, nullptr, &rest)) m.text = rest;
    }
    if (stopReason == "refusal") {
        m.note = "La demande a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "clin\xC3\xA9" "e par le mod\xC3\xA8le : rien n'est propos\xC3\xA9.";
    } else if (stopReason == "max_tokens") {
        m.note = "La r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e (la limite de longueur est atteinte) : aucune op\xC3\xA9ration n'est propos\xC3\xA9" "e. "
                 "Demande moins d'objets \xC3\xA0 la fois, ou d\xC3\xA9" "coupe la demande.";
    }
    if (gen_.tool) {
        m.tool = *gen_.tool;
        if (m.tool->name != kOperationsTool) {
            m.tool->error = "outil inconnu : " + m.tool->name + " (seul " + std::string(kOperationsTool) + " existe)";
        } else {
            const auto doc = host_.document ? host_.document() : nullptr;
            PromptInput in;
            in.project = doc ? &doc->project : nullptr;
            in.view = doc && gen_.viewId != hmi::kNoId ? doc->project.view(gen_.viewId) : nullptr;
            in.settings = settings_;
            in.env = host_.env;
            in.projectName = host_.projectName ? host_.projectName() : std::string{};
            in.request = gen_.request;
            std::string why;
            auto p = proposalFromTool(m.tool->input, in, gen_.request, &why);
            if (!p) {
                m.tool->error = why.empty() ? std::string("entr\xC3\xA9" "e illisible") : why;
            } else {
                // L'empreinte de la vue A L'ENVOI : si l'utilisateur l'a changee pendant la reponse,
                // la proposition est perimee (elle ne s'applique jamais sur une vue changee).
                // 1.16.0 : avec ce que la proposition cree (proposalPrint) ; une vue neuve ne depend pas de la vue ouverte.
                if (!gen_.fingerprint.empty() && doc) p->fingerprint = proposalPrint(doc->project, *p, gen_.fingerprint);
                if (p->summary.empty()) p->summary = "Proposition de l'Assistant";
                if (p->request.empty()) p->request = gen_.request;
                p->state = "proposee";
                if (doc) {
                    const Check ck = validate(doc->project, *p, host_.env);
                    p->create = ck.create;
                    p->modify = ck.modify;
                    p->remove = ck.remove;
                }
                m.proposal = std::move(*p);
            }
        }
        if (!m.tool->error.empty())
            m.note = "La proposition re\xC3\xA7ue est illisible, rien n'est propos\xC3\xA9 : " + m.tool->error + " (l'IA le saura \xC3\xA0 ta prochaine demande).";
    }
    if (m.text.empty() && !m.proposal && m.note.empty()) m.note = "R\xC3\xA9ponse vide.";
    const int index = gen_.message;
    const bool open = current() && current()->id == c->id;
    const bool hasProposal = m.proposal.has_value();
    gen_ = {};
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    if (hasProposal && open && directMode()) autoApply(index);
}

void Assistant::failLive(const ProviderEvent& e) {
    retire();
    recordLive();
    Conversation* c = generatingConversation();
    if (c && gen_.message >= 0 && gen_.message < static_cast<int>(c->messages.size())) {
        Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
        m.text = trim(gen_.text);
        if (!gen_.usage.empty()) m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total()));   // 1.16.0
        m.note = e.text.empty() ? std::string("Erreur inconnue.") : e.text;
        m.state = "erreur";
        m.date = isoDate(now());
        m.provider = gen_.provider;
        m.model = gen_.model;
        gen_ = {};
        if ((c = touch(*c)) != nullptr) save(*c);
    } else {
        gen_ = {};
    }
    notice_ = "Assistant IA : " + (e.text.empty() ? std::string("erreur") : e.text);
    ++revision_;
}

bool Assistant::canRetry() const {
    if (!live() || generating()) return false;
    const auto* c = current();
    if (!c || c->messages.size() < 2) return false;
    const Message& last = c->messages.back();
    if (last.role != "assistant") return false;
    const bool failed = last.state == "erreur" || (last.state == "arrete" && !last.provider.empty());
    return failed && c->messages[c->messages.size() - 2].role == "utilisateur";
}

bool Assistant::retry(std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!canRetry()) return fail("Rien \xC3\xA0 r\xC3\xA9" "essayer.");
    std::string w;
    if (!ready(&w)) return fail(w);
    Conversation& c = *cur();
    const Message last = c.messages.back();
    c.messages.pop_back();
    const std::string request = c.messages.back().text;
    if (!startLive(c, request, &w)) {
        c.messages.push_back(last);
        return fail(w);
    }
    suggestionsAfter_ = false;
    if (Conversation* cc = touch(c)) save(*cc);
    ++revision_;
    return true;
}

std::string Assistant::outgoingSummary() const {
    // Rien a dire : la demonstration, un envoi impossible (a configurer), ou une reponse en cours.
    if (!settings_.enabled || !live() || generating() || !ready()) return {};
    if (const auto* c = current())
        for (const auto& m : c->messages)
            if (!m.provider.empty()) return {};   // deja envoyee : dit une fois
    const DemoContext ctx = context();
    std::string s = "Partira chez " + providerName(settings_.provider) + " : ta demande";
    if (settings_.sendView) {
        if (ctx.view) s += ", la vue \xC2\xAB " + ctx.view->name + " \xC2\xBB (" + plural(ctx.view->objects.size(), "objet", "objets") + ")";
    } else {
        s += ", pas la vue";
    }
    if (settings_.sendSelection && !ctx.selection.empty()) s += ", la s\xC3\xA9lection (" + std::to_string(ctx.selection.size()) + ")";
    if (settings_.sendVariables && ctx.project) s += ", " + plural(ctx.project->programs.variables.size(), "variable", "variables");
    if (!pending_.empty())
        s += settings_.sendAttachments ? ", " + plural(pending_.size(), "pi\xC3\xA8" "ce jointe", "pi\xC3\xA8" "ces jointes")
                                       : std::string(", pas les pi\xC3\xA8" "ces jointes");
    return s + ".";
}

// ---- la liste des modeles --------------------------------------------------------------------------
bool Assistant::queryModels(const Settings& s, const std::string& key, std::string* why) {
    if (!modelQuery_) modelQuery_ = std::make_unique<ModelQuery>();
    if (s.provider == "openai") {
        // 1.16.0 : un service compatible OpenAI - GET <adresse>/models (la cle en Authorization, si donnee).
        OpenAITarget t;
        if (!openAITarget(s.endpoint, s.preset, !key.empty(), t, why)) return false;
        if (!modelQuery_->startUrl(t.models, openAIHeaders(key), s.timeoutSeconds, true, t.privateHttp && key.empty(), why)) return false;
    } else if (!modelQuery_->start(s.endpoint, key, s.timeoutSeconds, why)) {
        return false;
    }
    modelsProvider_ = s.provider;
    models_.reset();
    ++revision_;
    return true;
}

bool Assistant::pollModels() {
    if (!modelQuery_) return false;
    auto r = modelQuery_->poll();
    if (!r) return false;
    // 1.16.0 : les fenetres de contexte que le service dit (la fenetre "Auto").
    for (const auto& m : r->models)
        if (m.contextTokens > 0) modelContext_[modelsProvider_ + "|" + m.id] = m.contextTokens;
    models_ = std::move(*r);
    ++modelsRev_;
    ++revision_;
    return true;
}

// ---- 1.16.0 (FOURNISSEURS) : la limite et la consommation en direct, le budget -----------------------
void Assistant::recordLive() {
    if (gen_.usage.empty() || gen_.provider.empty()) return;
    recordUsage(gen_.provider, gen_.usage, gen_.conversation);
    limit_.last = gen_.usage;
    limit_.contextUsed = gen_.usage.input + gen_.usage.cacheRead + gen_.usage.cacheWrite;
    limit_.answered = true;
    if (limit_.provider.empty()) limit_.provider = gen_.provider;
    totalsDay_.clear();   // relus au prochain affichage
}

const UsageTotals& Assistant::usageTotals() const {
    // Relus apres une reponse, et quand le jour change (minuit pendant la seance).
    const std::string day = isoDate(now()).substr(0, 10);
    if (day != totalsDay_) {
        totals_ = app::ai::usageTotals();
        totalsDay_ = day;
    }
    return totals_;
}

bool Assistant::budgetBlocks(std::string* question) const {
    if (!live() || overBudgetOk_) return false;   // confirme : le prochain envoi passe
    return budgetExceeded(settings_, usageTotals(), question);
}

int Assistant::contextWindow() const {
    if (settings_.contextTokens > 0) return settings_.contextTokens;
    const auto it = modelContext_.find(settings_.provider + "|" + trim(settings_.model));
    return it == modelContext_.end() ? 0 : static_cast<int>(std::min<long long>(it->second, 100000000));
}

bool Assistant::toolsInText() const {
    if (settings_.provider != "openai") return false;   // Anthropic : toujours l'appel d'outil
    if (settings_.toolMode == "texte") return true;
    return settings_.toolMode == "auto" && noTools_ == settings_.provider + "|" + trim(settings_.endpoint) + "|" + trim(settings_.model);
}

bool Assistant::localService() const { return settings_.provider == "openai" && app::ai::localService(settings_.endpoint, settings_.preset); }

UsageBand Assistant::usageBand(double now) const {
    LimitState st = limit_;
    st.conversation = 0;
    if (const auto* c = current())
        for (const auto& m : c->messages) st.conversation += m.extra.integer("jetons");
    if (gen_.live && gen_.conversation == (current() ? current()->id : std::string{})) st.conversation += gen_.usage.total();   // en cours
    st.contextWindow = contextWindow();
    return app::ai::usageBand(settings_, st, usageTotals(), localService(), now);
}

} // namespace app::ai
