// =============================================================================
//  app/ai/AiAssistant.cpp - voir AiAssistant.hpp
// =============================================================================
#include "AiAssistant.hpp"
#include "AiLearning.hpp"       // 1.17.0 : les mesures de l'IA integree
#include "AiLearningFile.hpp"   // 1.17.0 : ce que l'installateur a depose

#include "AiAutomate.hpp"

#include "AiHttp.hpp"
#include "AiKeys.hpp"          // 1.18.0 : le gestionnaire des cles API
#include "AiLocal.hpp"
#include "AiOpenAI.hpp"
#include "AiPaths.hpp"
#include "AiPrompt.hpp"
#include "AiAgentTools.hpp"   // 1.21.0 : les outils de l'agent (analyser_projet, executer_code)
#include "AiSecret.hpp"

#include "../../core/Edition.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <memory>
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

Assistant::Assistant(Host host) : host_(std::move(host)) {
    // 1.18.0 : creer_ressource - une piece jointe de la conversation ouverte, par son nom (le plus recent d'abord) ; son chemin
    // (copiee dans IA/pieces-jointes/, ou le fichier d'origine sans racine). Un hote qui donne deja la sienne la garde.
    if (!host_.env.attachmentPath)
        host_.env.attachmentPath = [this](std::string_view name) -> std::string {
            const Conversation* c = current();
            if (!c) return {};
            for (auto m = c->messages.rbegin(); m != c->messages.rend(); ++m)
                for (const auto& a : m->attachments) {
                    if (a.name != name) continue;
                    if (!a.file.empty() && !root().empty()) return detail::join(root(), a.file);
                    if (!a.source.empty()) return a.source;
                }
            return {};
        };
}

// Le fournisseur en cours est arrete et part finir sur son fil (son destructeur attend le fil :
// jamais sur celui de l'interface tant qu'il travaille - tick le libere quand il a fini).
Assistant::~Assistant() {
    // 1.20.1 : une application de fond finit sur sa copie (elle ne touche rien d'autre) : attendue ici.
    if (applyJob_ && applyJob_->worker.joinable()) applyJob_->worker.join();
}

// ---- le cycle de vie ------------------------------------------------------------------
void Assistant::load(std::vector<std::string>* problems) {
    bool found = false;
    settings_ = loadSettings(&found);
    // 1.15.0 : la cle memorisee (protegee par Windows) revient pour la seance. 1.18.0 : celle du gestionnaire des cles
    // (l'active du fournisseur choisi) ; la cle d'avant (IA/cle.bin) y entre au premier lancement.
    if (apiKey_.empty()) {
        (void)keys::migrateLegacy(settings_.provider);
        (void)reloadKey();
        if (apiKey_.empty() && hasStoredKey()) {   // illisible a la migration (une autre session) : comme avant
            std::string why;
            if (auto key = loadStoredKey(&why)) apiKey_ = std::move(*key);
            else if (!why.empty()) notice_ = "Assistant IA : " + why;
        }
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
    // 1.17.0 : CE QUE L'INSTALLATEUR A DEPOSE (le dossier commun des donnees du programme) - le fournisseur choisi a
    // l'installation, un apprentissage d'un autre PC : applique une fois par utilisateur, puis le panneau le dit.
    {
        std::string said;
        if (learning::applyInstallation(&said)) {
            settings_ = loadSettings();
            if (!said.empty()) notice_ = said;
        }
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
    pollApply();   // 1.20.1 : l'application de fond finie : sa commande, sur ce fil
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
    // 1.16.1 : L'IA INTEGREE D'ABORD (Settings::localFirst) - ce qu'elle comprend est repondu tout de suite, sans
    // connexion ni jeton ; l'IA connectee ne recoit que ce qu'elle ne comprend pas. Une piece jointe va a l'IA
    // connectee (elle la lit).
    std::optional<LocalReply> localFirst;
    if (live() && settings_.localFirst && pending_.empty() && !text.empty()) {
        const auto keep = host_.document ? host_.document() : nullptr;   // tenu le temps de la lecture
        const Conversation* c0 = cur();
        LocalReply lr = askLocal(c0, c0 ? c0->messages.size() : 0, context(), text);
        // 1.17.0 : la mesure (comprise ici, ou partie a l'IA connectee) - l'onglet Apprentissage en tire ses courbes
        learning::recordReply(lr.understood, text, lr.understood && lr.proposal.has_value());
        if (lr.understood) localFirst = std::move(lr);
    }
    if (live() && !localFirst) {
        // 1.15.0 : un vrai fournisseur. Rien ne change dans la conversation si l'envoi ne part pas.
        std::string w;
        if (!ready(&w)) return fail(w);
        localSuggestions_.clear();
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
        if (first || c.title == kNewTitle) c.title = text.empty() ? std::string("Pi\xC3\xA8" "ces jointes") : titleFor(text, settings_.provider == "demo");
        if (first) {
            c.project.name = host_.projectName ? host_.projectName() : std::string{};
            c.project.path = host_.projectPath ? host_.projectPath() : std::string{};
        }
        if (const DemoContext ctx = context(); ctx.view) c.project.view = ctx.view->name;
        const std::string plcRead = readPlcAttachments(u.attachments);   // avant le contexte de la demande
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
        if (!plcRead.empty()) {
            std::string plain = plcRead;
            std::erase(plain, '*');
            std::erase(plain, '`');
            while (!plain.empty() && (plain.back() == '\n' || plain.back() == ' ')) plain.pop_back();
            notice_ = "Assistant IA : " + plain;
        }
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
    if (first || c.title == kNewTitle) c.title = text.empty() ? std::string("Pi\xC3\xA8" "ces jointes") : titleFor(text, settings_.provider == "demo");
    if (first) {
        c.project.name = host_.projectName ? host_.projectName() : std::string{};
        c.project.path = host_.projectPath ? host_.projectPath() : std::string{};
    }
    if (ctx.view) c.project.view = ctx.view->name;
    c.messages.push_back(std::move(u));

    // 1.16.0 (captures) : un .XPG joint est lu d'abord (l'automate de la seance : ses variables et ses DDT).
    const std::string plcRead = readPlcAttachments(ctx.attachments);
    // 1.16.0 : l'IA integree (sur ce PC, des regles) ; sinon la demonstration.
    DemoReply reply;
    localSuggestions_.clear();
    const bool byLocal = local() || localFirst.has_value();
    if (byLocal) {
        // les tours d'avant : ceux de la conversation, sans la demande qu'on vient d'y mettre
        const bool measured = localFirst.has_value();   // deja mesuree (le relais)
        LocalReply lr = localFirst ? std::move(*localFirst) : askLocal(&c, c.messages.size() - 1, ctx, text);
        if (!measured && !text.empty()) learning::recordReply(lr.understood, text, lr.proposal.has_value());   // 1.17.0
        if (localFirst)
            lr.text += "\n\n*(R\xC3\xA9pondu par l'IA int\xC3\xA9gr\xC3\xA9" "e, sans connexion. Ce qu'elle ne comprend pas part \xC3\xA0 l'IA connect\xC3\xA9" "e.)*";
        reply.text = std::move(lr.text);
        reply.proposal = std::move(lr.proposal);
        // 1.16.0 (captures) : l'empreinte d'avant, celle de ce que la proposition touche (la vue ouverte, ou la vue
        // neuve encore absente, les variables et types crees) - la carte saura dire qu'un Ctrl+Z l'a reprise.
        if (reply.proposal && ctx.project) {
            scopeProposal(*reply.proposal, ctx.view);   // 1.19.0 : la vue ouverte d'abord, sinon la vue neuve principale
            reply.proposal->fingerprint = proposalPrint(*ctx.project, *reply.proposal, ctx.view ? fingerprint(*ctx.view) : std::string{});
        }
        reply.suggestions = !lr.suggestions.empty();
        localSuggestions_ = std::move(lr.suggestions);
        std::string files;
        for (const auto& att : ctx.attachments)
            if (!host_.readPlc || !isPlcExport(att.name)) files += (files.empty() ? "" : ", ") + std::string("**") + att.name + "**";
        if (!files.empty())
            reply.text = "J'ai bien re\xC3\xA7u " + files + ", mais l'IA int\xC3\xA9gr\xC3\xA9" "e ne lit pas le contenu des pi\xC3\xA8" "ces jointes "
                         "(sauf l'export de l'automate, .XPG).\n\n" + reply.text;
        reply.text = plcRead + reply.text;
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
    gen_.local = byLocal;
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

learning::Trial Assistant::tryPhrase(const std::string& phrase) const {
    const DemoContext ctx = context();
    if (!ctx.project) return {false, "Pas de projet ouvert : ouvre un projet d'IHM Studio pour essayer une phrase."};
    const LocalReply lr = askLocal(nullptr, 0, ctx, phrase);
    learning::Trial t;
    t.understood = lr.understood;
    // le resume : la proposition (et les membres d'un type) ; sinon la premiere ligne de la reponse, sans sa mise en forme
    if (lr.proposal) {
        t.summary = lr.proposal->summary;
        for (const auto& op : lr.proposal->ops)
            if (op.action == Operation::Action::Type && !op.members.empty()) {
                std::string list;
                for (std::size_t i = 0; i < op.members.size() && i < 6; ++i)
                    list += (i ? ", " : "") + op.members[i].name + " (" + op.members[i].type + ")";
                if (op.members.size() > 6) list += ", \xE2\x80\xA6";
                t.summary += " : " + list;
                break;
            }
    } else {
        std::string line = lr.text.substr(0, lr.text.find('\n'));
        std::string clean;
        for (const char ch : line)
            if (ch != '*' && ch != '`') clean += ch;
        if (clean.size() > 220) {
            std::size_t n = 220;
            while (n > 0 && (static_cast<unsigned char>(clean[n]) & 0xC0u) == 0x80u) --n;
            clean = clean.substr(0, n) + "\xE2\x80\xA6";
        }
        t.summary = clean;
    }
    if (!t.understood && t.summary.empty()) t.summary = "pas comprise";
    return t;
}

LocalReply Assistant::askLocal(const Conversation* c, std::size_t turns, const DemoContext& ctx, const std::string& text) const {
    PromptInput in;
    in.project = ctx.project;
    in.view = ctx.view;
    in.selection = ctx.selection;
    in.settings = settings_;
    in.env = host_.env;
    in.projectName = host_.projectName ? host_.projectName() : std::string{};
    in.request = text;
    in.incidents = host_.incidents;   // 1.20.0 : les questions de diagnostic des incidents
    // les tours d'avant (les six derniers) : la demande, la proposition de sa reponse
    std::vector<LocalTurn> history;
    if (c)
        for (std::size_t i = 0; i < turns && i < c->messages.size(); ++i) {
            if (c->messages[i].role != "utilisateur") continue;
            LocalTurn turn;
            turn.request = c->messages[i].text;
            if (i + 1 < turns && c->messages[i + 1].role == "assistant") turn.proposal = c->messages[i + 1].proposal;
            history.push_back(std::move(turn));
        }
    if (history.size() > 6) history.erase(history.begin(), history.end() - 6);
    return understand(in, text, history);
}

std::string Assistant::readPlcAttachments(const std::vector<Attachment>& attachments) {
    std::string said;
    if (!host_.readPlc) return said;
    for (const auto& att : attachments) {
        if (!isPlcExport(att.name)) continue;
        std::string what;
        if (host_.readPlc(attachmentPath(att), &what)) {
            said += "J'ai lu l'automate de **" + att.name + "** : " + what + ". Ses variables ne s'associent pas directement : demande "
                    "`Cr\xC3\xA9" "e les variables de l'automate` (un morceau de nom les filtre) ou `Propose les types IHM des DDT`, puis "
                    "associe les variables IHM cr\xC3\xA9\xC3\xA9" "es.\n\n";
            appendActivity("fichier", "Automate lu : " + att.name + " (" + what + ")");
        } else {
            said += "Je n'ai pas pu lire l'automate de **" + att.name + "** : " + what + ".\n\n";
        }
    }
    return said;
}

std::string Assistant::shownText() const {
    if (gen_.live && gen_.textTool) {
        // 1.16.0 : la proposition en texte (un bloc ```json) ne s'ecrit pas dans la bulle : la carte la montre.
        // 1.20.2 : SEULEMENT elle (```json, ou un bloc sans langue qui commence par une accolade) - les autres blocs de
        // code (ST, C++...) s'ecrivent dans la bulle, avec leur bouton Copier.
        const std::string& t = gen_.text;
        std::size_t at = 0;
        while ((at = t.find("```", at)) != std::string::npos) {
            const std::size_t lineEnd = t.find('\n', at + 3);
            std::string tag;
            for (const char ch : std::string_view(t).substr(at + 3, lineEnd == std::string::npos ? std::string::npos : lineEnd - at - 3))
                if (ch != ' ' && ch != '\t' && ch != '\r') tag += static_cast<char>(ch >= 'A' && ch <= 'Z' ? ch - 'A' + 'a' : ch);
            if (lineEnd == std::string::npos)   // la langue arrive : cachee tant qu'elle peut devenir << json >>
                return std::string_view("json").substr(0, tag.size()) == tag ? t.substr(0, at) : t;
            if (tag == "json") return t.substr(0, at);
            if (tag.empty()) {
                const std::size_t q = t.find_first_not_of(" \t\r\n", lineEnd + 1);
                if (q == std::string::npos || t[q] == '{') return t.substr(0, at);
            }
            const std::size_t close = t.find("```", lineEnd + 1);   // un autre bloc : montre, jusqu'a sa fin
            if (close == std::string::npos) return t;
            at = close + 3;
        }
        return t;
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

bool Assistant::frozenEnvironment(Env& out) const {
    std::map<std::string, std::string> files;   // comme attachmentPath : le plus recent d'abord
    if (const Conversation* c = current())
        for (auto m = c->messages.rbegin(); m != c->messages.rend(); ++m)
            for (const auto& a : m->attachments) {
                if (files.count(a.name)) continue;
                if (!a.file.empty() && !root().empty()) files[a.name] = detail::join(root(), a.file);
                else if (!a.source.empty()) files[a.name] = a.source;
            }
    return frozenEnv(host_.env, files, out);
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

bool Assistant::apply(int message, std::string* why) { return applyWith(message, why, false); }

bool Assistant::applyIgnoringErrors(int message, std::string* why) {
    if (!settings_.applyWithErrors) {
        if (why) *why = "Appliquer malgr\xC3\xA9 les erreurs est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres de l'Assistant IA).";
        return false;
    }
    return applyWith(message, why, true);
}

bool Assistant::applyWith(int message, std::string* why, bool ignoringErrors) {
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
    // 1.19.0 : le projet d'avant et le plan valide - le compte rendu se lit dans la difference avant / apres.
    std::optional<hmi::Project> before;
    plan::Plan pl;
    if (doc) {
        before = doc->project;
        pl = plan::build(doc->project, *p, validate(doc->project, *p, host_.env));
    }
    std::string w;
    std::vector<std::string> skipped;   // 1.16.1 : ce qui est laisse de cote (appliquer malgre les erreurs)
    auto cmd = ignoringErrors ? makeCommandLenient(doc, *p, skipped, &w, host_.env) : makeCommand(doc, *p, &w, host_.env);
    if (!cmd) {
        if (why) *why = w;
        notice_ = w;
        // 1.19.0 : une transaction refusee - le projet n'a pas change (rien n'a ete applique), dit dans le journal.
        appendActivity("conversation", "Proposition non appliqu\xC3\xA9" "e : " + w);
        ++revision_;
        return false;
    }
    if (host_.apply) host_.apply(std::move(cmd));
    return afterApply(*c, message, pl, before, skipped);
}

// La suite d'une application (sur le fil de l'interface) : l'etat, le compte rendu reel, l'empreinte d'apres, la note.
bool Assistant::afterApply(Conversation& conv, int message, plan::Plan pl, const std::optional<hmi::Project>& before,
                           const std::vector<std::string>& skipped) {
    Conversation* c = &conv;
    auto& prop = *c->messages[static_cast<std::size_t>(message)].proposal;
    prop.state = "appliquee";
    // 1.19.0 : LE COMPTE RENDU REEL - les etats confirmes dans le projet d'apres (jamais ce que l'IA a annonce).
    if (const auto after = host_.document ? host_.document() : nullptr; after && before) {
        const plan::Report rep = plan::report(*before, after->project, pl, prop);
        prop.plan = plan::toJson(pl);
        prop.report = plan::reportToJson(rep);
        if (pl.worthShowing()) {
            appendActivity("conversation", "Plan appliqu\xC3\xA9 : " + rep.verdict);
            for (const auto& f : rep.failures) appendActivity("conversation", "\xC3\x89tape non appliqu\xC3\xA9" "e : " + f);
        }
    }
    // L'empreinte d'apres : la carte saura dire si un Ctrl+Z l'a reprise. 1.16.0 : proposalPrint (la vue
    // neuve, les variables et les types crees comptent) ; la vue neuve retient son identifiant.
    prop.fingerprintAfter.clear();
    hmi::Id opened = hmi::kNoId;
    if (const auto after = host_.document ? host_.document() : nullptr) {
        // 1.19.0 : une proposition qui touche AUSSI la vue ouverte garde son identifiant ; la vue neuve s'ouvre quand meme.
        if (createsView(prop))
            if (const auto* nv = after->project.viewByName(newViewName(prop))) {
                opened = nv->id;
                if (!usesOpenView(prop)) prop.viewId = nv->id;
            }
        prop.fingerprintAfter = proposalPrint(after->project, prop);
    }
    notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e (Ctrl+Z l'annule d'un coup).";
    if (const std::string made = creationSummary(prop); !made.empty())
        notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e - cr\xC3\xA9\xC3\xA9" "s : " + made + " (Ctrl+Z reprend tout d'un coup).";
    // 1.16.1 : les erreurs laissees de cote - sous la reponse (la note du logiciel) et dans le journal d'activite.
    if (!skipped.empty()) {
        std::string list;
        for (const auto& e : skipped) list += "\n- " + e;
        auto& msg = c->messages[static_cast<std::size_t>(message)];
        msg.note += (msg.note.empty() ? "" : "\n") + std::string("Appliqu\xC3\xA9" "e malgr\xC3\xA9 ") + std::to_string(skipped.size())
                    + (skipped.size() > 1 ? " erreurs, laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9 :" : " erreur, laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 :") + list;
        for (const auto& e : skipped) appendActivity("conversation", "Appliqu\xC3\xA9" "e malgr\xC3\xA9 l'erreur (laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9) : " + e);
        notice_ = "Assistant IA : " + prop.summary + " appliqu\xC3\xA9" "e, " + std::to_string(skipped.size())
                  + (skipped.size() > 1 ? " erreurs laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9 (sous la r\xC3\xA9ponse et dans le journal)."
                                        : " erreur laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9 (sous la r\xC3\xA9ponse et dans le journal).");
    }
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    // 1.16.0 : la vue creee s'ouvre dans l'editeur.
    if (opened != hmi::kNoId && host_.openView) host_.openView(opened);
    return true;
}

bool Assistant::applyInBackground(int message, bool ignoringErrors, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (applyJob_) return fail("Une autre proposition s'applique d\xC3\xA9j\xC3\xA0 : attends la fin.");
    if (ignoringErrors && !settings_.applyWithErrors)
        return fail("Appliquer malgr\xC3\xA9 les erreurs est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres de l'Assistant IA).");
    const Conversation* c = current();
    const Proposal* p = proposal(message);
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!c || !p || !doc || p->state != "proposee" || p->ops.size() < kBackgroundOps) return false;
    Env frozen;
    if (!frozenEnvironment(frozen)) return false;
    auto job = std::make_shared<ApplyJob>();
    job->conversation = c->id;
    job->proposalId = p->id;
    job->message = message;
    job->ignoringErrors = ignoringErrors;
    job->revision = doc->revision;
    job->before = std::make_shared<const hmi::Project>(doc->project);
    job->total = 3 * p->ops.size();   // la validation du plan, celle de l'application, l'application
    job->worker = std::thread([job, prop = *p, env = std::move(frozen)] {
        setProgressCounter(&job->done);
        job->plan = plan::build(*job->before, prop, validate(*job->before, prop, env));
        job->result = appliedCopy(*job->before, prop, env, &job->why, job->ignoringErrors ? &job->skipped : nullptr);
        setProgressCounter(nullptr);
        job->finished.store(true, std::memory_order_release);
    });
    applyJob_ = std::move(job);
    notice_ = "Assistant IA : la proposition s'applique en arri\xC3\xA8re-plan (" + std::to_string(p->ops.size())
              + " op\xC3\xA9rations) - tu peux continuer \xC3\xA0 travailler.";
    ++revision_;
    return true;
}

bool Assistant::applying(int message) const {
    const Conversation* c = current();
    return applyJob_ && c && c->id == applyJob_->conversation && applyJob_->message == message;
}

float Assistant::applyProgress() const {
    if (!applyJob_ || applyJob_->total == 0) return 0.f;
    return std::min(1.f, static_cast<float>(applyJob_->done.load()) / static_cast<float>(applyJob_->total));
}

void Assistant::pollApply() {
    if (!applyJob_ || !applyJob_->finished.load(std::memory_order_acquire)) return;
    auto job = std::move(applyJob_);
    if (job->worker.joinable()) job->worker.join();
    ++revision_;
    Conversation* c = nullptr;
    for (auto& conv : conversations_)
        if (conv.id == job->conversation) c = &conv;
    if (!c || job->message < 0 || job->message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(job->message)].proposal
        || c->messages[static_cast<std::size_t>(job->message)].proposal->id != job->proposalId
        || c->messages[static_cast<std::size_t>(job->message)].proposal->state != "proposee") {
        notice_ = "Assistant IA : la proposition a chang\xC3\xA9 pendant son application en arri\xC3\xA8re-plan : rien n'est appliqu\xC3\xA9.";
        return;
    }
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc || doc->revision != job->revision) {
        notice_ = "Assistant IA : le projet a chang\xC3\xA9 pendant l'application en arri\xC3\xA8re-plan : rien n'est appliqu\xC3\xA9 "
                  "(Appliquer la refait sur le projet tel qu'il est).";
        appendActivity("conversation", "Proposition non appliqu\xC3\xA9" "e : le projet a chang\xC3\xA9 pendant son application en arri\xC3\xA8re-plan.");
        return;
    }
    if (!job->result) {
        notice_ = job->why.empty() ? std::string("La proposition ne s'applique pas.") : job->why;
        appendActivity("conversation", "Proposition non appliqu\xC3\xA9" "e : " + notice_);
        return;
    }
    std::string w;
    auto cmd = commandSetting(doc, *c->messages[static_cast<std::size_t>(job->message)].proposal, job->result, &w);
    if (!cmd) {
        notice_ = w;
        return;
    }
    if (host_.apply) host_.apply(std::move(cmd));
    (void)afterApply(*c, job->message, job->plan, std::optional<hmi::Project>(*job->before), job->skipped);
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

plan::Plan Assistant::planOf(int message) const {
    const Proposal* p = proposal(message);
    if (!p) return {};
    if (p->state == "appliquee" && p->plan.isObject()) {
        plan::Plan pl = plan::fromJson(p->plan);
        // Ctrl+Z : ce qui etait applique est annule (la carte le dit ; le compte rendu garde ce qui avait ete fait).
        if (appliedOf(*p) == Applied::Undone)
            for (auto& s : pl.steps)
                if (s.state == plan::State::Applied) s.state = plan::State::Cancelled;
        return pl;
    }
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return plan::build(hmi::Project{}, *p, Check{});
    return plan::build(doc->project, *p, validate(doc->project, *p, host_.env));
}

std::optional<plan::Report> Assistant::reportOf(int message) const {
    const Proposal* p = proposal(message);
    if (!p || p->state != "appliquee" || !p->report.isObject()) return std::nullopt;
    return plan::reportFromJson(p->report);
}

bool Assistant::setExcluded(int message, std::size_t op, bool excluded) {
    auto* c = cur();
    if (!c || message < 0 || static_cast<std::size_t>(message) >= c->messages.size()) return false;
    auto& m = c->messages[static_cast<std::size_t>(message)];
    if (!m.proposal || m.proposal->state != "proposee" || op >= m.proposal->ops.size()) return false;
    auto& ex = m.proposal->excluded;
    const auto it = std::find(ex.begin(), ex.end(), op);
    if (excluded == (it != ex.end())) return true;
    if (excluded) ex.push_back(op);
    else ex.erase(it);
    std::sort(ex.begin(), ex.end());
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
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

bool Assistant::unrefuse(int message) {
    auto* c = cur();
    const Proposal* p = proposal(message);
    if (!c || !p || p->state != "refusee") return false;
    c->messages[static_cast<std::size_t>(message)].proposal->state = "proposee";
    notice_ = "Refus annul\xC3\xA9 : la proposition est de nouveau \xC3\xA0 pr\xC3\xA9visualiser, appliquer ou refuser.";
    save(*c);
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
    // 1.16.0 : un autre service (fournisseur, adresse) - la limite lue sur l'ancien ne vaut plus.
    if (s.provider != settings_.provider || trim(s.endpoint) != trim(settings_.endpoint) || s.preset != settings_.preset) {
        const long long conversation = limit_.conversation;
        limit_ = LimitState{};
        limit_.conversation = conversation;
    }
    // 1.18.0 : un autre fournisseur - sa cle active (le gestionnaire des cles). L'appelant qui pose une cle saisie
    // (Parametres) la pose ensuite.
    const bool otherKey = keys::providerOf(s.provider) != keys::providerOf(settings_.provider);
    settings_ = std::move(s);
    (void)saveSettings(settings_);
    if (otherKey) {
        apiKey_.clear();
        (void)reloadKey();
    }
    ++revision_;
}

bool Assistant::reloadKey() {
    const std::string provider = keys::providerOf(settings_.provider);
    if (provider.empty()) return true;
    const std::string id = keys::activeId(provider);
    if (id.empty()) return true;
    std::string why;
    if (auto key = keys::secret(id, &why)) {
        apiKey_ = std::move(*key);
        ++revision_;
        return true;
    }
    const auto e = keys::find(id);
    notice_ = "Assistant IA : la cl\xC3\xA9 \xC2\xAB " + (e ? e->name : id) + " \xC2\xBB ne se relit pas sur ce poste ("
              + (why.empty() ? std::string("une autre session Windows, un autre PC") : why)
              + ") : saisis-la de nouveau (Param\xC3\xA8tres \xE2\x80\xBA G\xC3\xA9rer les cl\xC3\xA9s).";
    ++revision_;
    return false;
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
    // 1.16.0 (captures) : une IA en ligne ou sur ce PC - des demandes ordinaires (celles de la demonstration citent
    // des objets et une variable de son scenario).
    if (live()) {
        // 1.16.1 : la derniere reponse venait de l'IA integree (essayee d'abord) - ses suggestions de suite
        if (settings_.localFirst && suggestionsAfter_ && !localSuggestions_.empty()) return localSuggestions_;
        static const std::vector<std::string> k = {
            "Cr\xC3\xA9" "e un synoptique de station de pompage",
            "Ajoute une cuve avec un indicateur de niveau",
            "Cr\xC3\xA9" "e une vue avec 3 pompes et leurs voyants",
            "Aligne les boutons en haut",
            "Que sais-tu faire ?",
        };
        return k;
    }
    if (!local()) return demoSuggestions();
    return suggestionsAfter_ && !localSuggestions_.empty() ? localSuggestions_ : localSuggestions();
}

// ---- la cle ------------------------------------------------------------------------------------
bool Assistant::rememberKey(bool on, std::string* why) {
    ++revision_;
    // 1.18.0 : la cle active du fournisseur, dans le gestionnaire des cles (IA/cles/).
    const std::string provider = keys::providerOf(settings_.provider);
    const std::string id = provider.empty() ? std::string{} : keys::activeId(provider);
    if (!on) {
        (void)forgetStoredKey();   // la cle d'avant, s'il en reste une
        if (id.empty()) return true;
        if (apiKey_.empty()) return keys::remove(id);
        return keys::replace(id, apiKey_, false, why);   // gardee pour la seance seulement
    }
    if (apiKey_.empty()) {
        if (why) *why = "Pas de cl\xC3\xA9 \xC3\xA0 m\xC3\xA9moriser.";
        return false;
    }
    if (provider.empty()) return storeKey(apiKey_, why);
    if (!id.empty()) {
        const auto cur = keys::secret(id);
        const auto e = keys::find(id);
        if (cur && *cur == apiKey_ && e && e->stored) return true;
        return keys::replace(id, apiKey_, true, why);
    }
    keys::Entry e;
    e.provider = provider;
    e.name = "Ma cl\xC3\xA9 (" + keys::providerLabel(provider) + ")";
    const auto all = keys::list();
    const auto taken = [&](const std::string& n) {
        return std::any_of(all.begin(), all.end(), [&](const keys::Entry& x) { return x.name == n; });
    };
    for (int n = 2; taken(e.name) && n < 100; ++n) e.name = "Ma cl\xC3\xA9 (" + keys::providerLabel(provider) + ") " + std::to_string(n);
    const std::string made = keys::add(e, apiKey_, true, why);
    if (made.empty()) return false;
    (void)keys::setActive(made);
    return made.front() == 'k';
}

bool Assistant::keyRemembered() const {
    const std::string provider = keys::providerOf(settings_.provider);
    if (!provider.empty())
        if (const std::string id = keys::activeId(provider); !id.empty())
            if (const auto e = keys::find(id)) return e->stored;
    return hasStoredKey();
}

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
    in.incidents = host_.incidents;   // 1.20.0 : un resume borne des incidents, si la demande en parle
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
    // 1.21.0 : LES OUTILS DE L'AGENT (AiAgentTools.hpp) - IHM Studio, avec l'appel d'outil : analyser_projet (le graphe,
    // en lecture seule) et executer_code (le bac a sable). Sur une COPIE du projet, prise ici sur le fil de l'interface : le
    // fil du fournisseur ne lit jamais le projet vivant ; ce qui modifie le projet reste une proposition a valider.
    if (core::hasIhm() && ctx.project && !req.toolsInText) {
        auto snap = std::make_shared<const hmi::Project>(*ctx.project);
        req.localTools.push_back(agent::analyseTool(snap));
        if (sandbox_) req.localTools.push_back(agent::sandboxTool(snap, sandboxProgram_));
    }
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
                // 1.20.0 : une partie d'un gros texte joint lue a part (l'etat le dit) ; ce qui n'est pas parti entier (dit).
                if (e.text == "lecture" || e.text == "outil") ph = Phase::Waiting;   // 1.21.0 : "outil" (le bac a sable)
                if (e.text == "note") {
                    notice_ = "Assistant IA : " + e.detail;
                    ++revision_;
                    break;
                }
                const std::string detail = e.text == "reprise" || e.text == "sans-outil" || e.text == "lecture" || e.text == "outil" ? e.detail
                                           : e.text == "connexion" && gen_.detail.rfind("Lecture de ", 0) != 0 ? gen_.detail
                                                                                                                : std::string{};
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
                if (e.preread) {   // 1.20.0 : une lecture prealable - ajoutee, pas remplacee
                    gen_.preread.input += e.usage.input;
                    gen_.preread.output += e.usage.output;
                    gen_.preread.cacheRead += e.usage.cacheRead;
                    gen_.preread.cacheWrite += e.usage.cacheWrite;
                } else if (!e.usage.empty()) {
                    gen_.usage = e.usage;
                }
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
    if (!gen_.usage.empty() || !gen_.preread.empty())
        m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total()));
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
        if (!gen_.usage.empty() || !gen_.preread.empty())
            m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total()));   // 1.16.0
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
    if ((gen_.usage.empty() && gen_.preread.empty()) || gen_.provider.empty()) return;
    ProviderUsage all = gen_.usage;   // 1.20.0 : la reponse et ses lectures prealables
    all.input += gen_.preread.input;
    all.output += gen_.preread.output;
    all.cacheRead += gen_.preread.cacheRead;
    all.cacheWrite += gen_.preread.cacheWrite;
    recordUsage(gen_.provider, all, gen_.conversation);
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
    if (gen_.live && gen_.conversation == (current() ? current()->id : std::string{})) st.conversation += gen_.usage.total() + gen_.preread.total();   // en cours
    st.contextWindow = contextWindow();
    return app::ai::usageBand(settings_, st, usageTotals(), localService(), now);
}

} // namespace app::ai
