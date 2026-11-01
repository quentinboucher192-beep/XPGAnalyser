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
#include "AiConversationIndex.hpp"   // 1.21.0 : chercher_conversations
#include "AiMemoryManager.hpp"       // 1.21.0 : la memoire persistante (proposer_souvenir, chercher_souvenirs)
#include "AiNotifications.hpp"       // 1.21.2 : l'onglet Notifications du panneau (ce qui s'est passe)
#include "AiSecret.hpp"
#include "AiTextParts.hpp"           // 1.22.0 : withTool (planifier_mission ajoute aux outils de la demande)

#include "../../core/Edition.hpp"
#include "../../hmi/HmiAudit.hpp"    // 1.22.0 : l'audit final d'une mission (hmi::audit::run, sur une copie)

#include <algorithm>
#include <cstdlib>
#include <utility>
#include <cctype>
#include <chrono>
#include <cmath>
#include <memory>
#include <thread>
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

// ---- 1.21.2 : LES NOTIFICATIONS DE L'ASSISTANT (AiNotifications.hpp) ----
//  Ce qui merite d'etre retrouve dans l'onglet Notifications : chaque source poste UN evenement (jamais par tick).
std::string guillemets(const std::string& s) { return "\xC2\xAB " + s + " \xC2\xBB"; }

// Une proposition qui ne s'applique pas (la transaction refusee, ou le projet change pendant l'application de fond).
void notifyApplyFailed(const std::string& conversation, int message, const Proposal& p, const std::string& why) {
    notifs::Item n;
    n.category = "Proposition";
    n.title = "Proposition non appliqu\xC3\xA9" "e" + (p.summary.empty() ? std::string{} : " : " + notifs::oneLine(p.summary, 90));
    n.detail = (why.empty() ? std::string("La proposition ne s'applique pas.") : notifs::oneLine(why, 300)) + " Le projet n'a pas chang\xC3\xA9.";
    n.tone = "error";
    n.button = "Voir la carte";
    n.action = notifs::toCard(conversation, message);
    n.key = "proposition-echec:" + p.id;
    (void)notifs::post(std::move(n));
}

// ---- 1.21.3 : LA LONGUEUR D'UNE REPONSE ----
// "64 000" (l'espace insecable des milliers).
std::string groupedTokens(int n) {
    std::string d = std::to_string(std::max(0, n)), out;
    for (std::size_t i = 0; i < d.size(); ++i) {
        if (i > 0 && (d.size() - i) % 3 == 0) out += "\xC2\xA0";
        out += d[i];
    }
    return out;
}

// La consigne dite au modele : sa longueur, et comment faire une grande demande (en plusieurs propositions, la suite dans
// << suite >>) ; `splitFrom` > 0 : la reprise d'une reponse coupee a cette longueur - une premiere partie seulement.
std::string lengthPrompt(int maxTokens, int splitFrom) {
    const int ops = std::max(10, (maxTokens - 2000) / 120);
    std::string s = "LONGUEUR D'UNE R\xC3\x89PONSE : au plus " + groupedTokens(maxTokens) + " jetons (ta r\xC3\xA9" "flexion comprise), soit environ "
                    + std::to_string(ops) + " op\xC3\xA9rations. Une demande plus grande se fait en PLUSIEURS PROPOSITIONS : propose d'abord une "
                    "partie compl\xC3\xA8te et utilisable seule (par exemple la vue et ses objets), et d\xC3\xA9" "cris dans le champ \"suite\" (de "
                    "l'outil, ou du bloc JSON) ce qui restera \xC3\xA0 faire (par exemple : les alarmes et la surveillance de la communication) ; "
                    "l'utilisateur demandera la suite apr\xC3\xA8s avoir appliqu\xC3\xA9. Ne commence jamais une proposition que tu ne peux pas finir.";
    if (splitFrom > 0)
        s += "\nREPRISE : ta r\xC3\xA9ponse pr\xC3\xA9" "c\xC3\xA9" "dente \xC3\xA0 cette demande d\xC3\xA9passait cette longueur (" + groupedTokens(splitFrom)
             + " jetons) et a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e : rien n'a \xC3\xA9t\xC3\xA9 propos\xC3\xA9. Propose cette fois UNE PREMI\xC3\x88RE PARTIE seulement (au plus "
               "environ " + std::to_string(std::max(10, ops / 2)) + " op\xC3\xA9rations), compl\xC3\xA8te et utilisable, avec \"suite\" pour le reste ; "
               "dis en une ou deux phrases le d\xC3\xA9" "coupage choisi.";
    return s;
}

// Le titre d'une erreur du service, d'apres sa reponse HTTP (le detail : le texte de l'erreur, en clair).
std::string serviceErrorTitle(int http) {
    if (http == 401 || http == 403) return "Cl\xC3\xA9 API refus\xC3\xA9" "e par le service";
    if (http == 429) return "Limite du service atteinte";
    if (http >= 500 && http <= 599) return "Service indisponible ou surcharg\xC3\xA9";
    return "L'Assistant n'a pas pu r\xC3\xA9pondre";
}

// ---- 1.22.0 : LES MISSIONS IA PERSISTANTES ----
bool listed(const std::vector<std::string>& list, std::string_view s) { return std::find(list.begin(), list.end(), s) != list.end(); }

std::string joined(const std::vector<std::string>& list, std::string_view sep) {
    std::string s;
    for (const auto& x : list) s += (s.empty() ? std::string{} : std::string(sep)) + x;
    return s;
}

// "S1 Creer les equipements, S2 Creer les variables" : les etapes nommees, dans l'ordre donne.
std::string stepsLine(const mission::Mission& m, const std::vector<std::string>& ids) {
    std::string s;
    for (const auto& id : ids) {
        const mission::Step* st = m.step(id);
        s += (s.empty() ? "" : ", ") + id + (st && !st->title.empty() ? " " + st->title : std::string{});
    }
    return s;
}

// LA TAILLE D'UNE PROPOSITION DE MISSION (le poids d'un lot, mission::nextBatch) : la regle de lengthPrompt (1.21.3 :
// (jetons - 2000) / 120 operations, au moins 10), dont la proposition prend environ 60 % - la marge pour le texte, la
// reflexion du modele et les operations plus longues que la moyenne (une vue et ses objets).
int missionMaxOps(int tokens) { return std::max(10, (tokens - 2000) / 120) * 3 / 5; }

// Ce qu'un appel de mission demandait, pour l'historique et les notifications ("La demande de proposition").
std::string callLabel(const std::string& purpose, const std::string& step) {
    if (purpose == "plan") return "La demande de plan";
    if (purpose == "actualisation") return "L'actualisation du plan";
    if (purpose == "correction") return "La correction de " + step;
    return "La demande de proposition";
}

// Le plan, en clair : le message du logiciel sous la reponse de l'IA (jamais renvoye au modele).
std::string planMessage(const mission::Mission& m, bool revised) {
    std::size_t live = 0;
    for (const auto& st : m.steps) live += st.state != mission::StepState::Cancelled;
    std::string s = std::string(revised ? "**Plan revu de la mission " : "**Plan de la mission ") + guillemets(m.name) + "** ("
                    + plural(live, "\xC3\xA9tape", "\xC3\xA9tapes") + ")";
    if (!m.analysis.summary.empty()) s += "\n\n" + m.analysis.summary;
    const auto list = [&s](const char* label, const std::vector<std::string>& v) {
        if (v.empty()) return;
        s += std::string("\n\n*") + label + "* :";
        for (const auto& x : v) s += "\n- " + x;
    };
    list("D\xC3\xA9j\xC3\xA0 conforme dans le projet", m.analysis.conform);
    list("\xC3\x80 modifier", m.analysis.toModify);
    list("\xC3\x80 compl\xC3\xA9ter", m.analysis.toComplete);
    list("\xC3\x80 cr\xC3\xA9" "er", m.analysis.toCreate);
    list("Impossible", m.analysis.impossible);
    s += "\n\n";
    int n = 0;
    for (const auto& st : m.steps) {
        if (st.state == mission::StepState::Cancelled) continue;
        s += std::to_string(++n) + ". **" + st.id + "** " + st.title + " \xE2\x80\x94 " + std::string(mission::categoryLabel(st.category));
        if (!st.deps.empty()) s += " (d\xC3\xA9pend de " + joined(st.deps, ", ") + ")";
        s += " \xC2\xB7 " + std::string(mission::stepLabel(st.state)) + "\n";
    }
    s += "\nRien n'est encore modifi\xC3\xA9 par ce plan. Prochaine action : " + mission::nextAction(m);
    return s;
}

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
    bridge_ = std::make_shared<agent::Bridge>();   // 1.21.0 : les outils de l'agent y deposent leurs demandes
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
    // 1.21.0 : les lectures de classeurs en cours (courtes, bornees)
    for (auto& l : loads_)
        if (l && l->worker.joinable()) l->worker.join();
    // 1.22.0 : les audits finals de missions en cours (sur leur copie du projet)
    for (auto& a : audits_)
        if (a && a->worker.joinable()) a->worker.join();
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
    // 1.22.0 : LES MISSIONS - relues de IA/missions (un fichier illisible repris de sa copie, ou laisse tel quel : dit) ;
    // celles d'un format plus recent se lisent sans jamais etre reecrites ; un appel a l'IA en cours a la fermeture : la
    // mission a ete interrompue - constate au premier tick ou le projet ouvert est connu (rien n'est rejoue).
    {
        mission::Loaded loaded = mission::loadAll();
        missions_ = std::move(loaded.missions);
        missionsReadOnly_.clear();
        missionsReadOnly_.insert(loaded.readOnly.begin(), loaded.readOnly.end());
        missionsToRecover_.clear();
        for (const auto& m : missions_)
            if (m.pending.active) missionsToRecover_.push_back(m.id);
        nextMission_ = {};
        missionDoc_ = nullptr;
        missionDocRev_ = 0;
        missionSeenDoc_ = nullptr;
        missionSeenRev_ = 0;
        for (const auto& p : loaded.problems) {
            if (problems) problems->push_back("Mission : " + p);
            appendActivity("mission", "Mission illisible : " + p);
        }
        if (!loaded.problems.empty())
            notice_ = "Assistant IA : " + plural(loaded.problems.size(), "fichier de mission illisible", "fichiers de mission illisibles") + " (" + loaded.problems.front() + ").";
        ++missionsRev_;
    }
    ++revision_;
}

bool Assistant::tick(double now) {
    lastNow_ = now;
    const std::uint64_t before = revision_;
    // 1.21.0 : les analyses des pieces jointes finies (le fil d'analyse), remises dans leur conversation ; les souvenirs
    // que l'IA a proposes pendant sa reponse - a valider par l'utilisateur, dit une fois.
    pollAnalyses();
    pollWorkbooks();   // 1.21.0 : les classeurs relus sur un fil (une ancienne conversation rouverte)
    if (const int n = proposedMemories_->exchange(0); n > 0) {
        notice_ = "Assistant IA : " + std::to_string(n) + (n > 1 ? " souvenirs propos\xC3\xA9s" : " souvenir propos\xC3\xA9")
                  + " \xC3\xA0 valider dans M\xC3\xA9moire et apprentissage \xE2\x80\xBA Souvenirs (rien n'est retenu sans ton accord).";
        ++revision_;
        // 1.21.2 : l'onglet Notifications - ils attendent ta validation (la meme conversation : une ligne, << x2 >>)
        const std::string conv = !gen_.conversation.empty() ? gen_.conversation : current() ? current()->id : std::string{};
        std::string title;
        for (const auto& c : conversations_)
            if (c.id == conv) title = c.title;
        notifs::Item note;
        note.category = "Souvenirs";
        note.title = n > 1 ? std::to_string(n) + " souvenirs propos\xC3\xA9s attendent ta validation"
                           : std::string("Un souvenir propos\xC3\xA9 attend ta validation");
        note.detail = std::string(n > 1 ? "L'IA les a propos\xC3\xA9s" : "L'IA l'a propos\xC3\xA9") + (title.empty() ? std::string{} : " dans " + guillemets(title))
                      + " : rien n'est retenu sans ton accord (M\xC3\xA9moire et apprentissage \xE2\x80\xBA Souvenirs).";
        note.button = "Valider les souvenirs";
        note.action = notifs::toSouvenirs("provisoire");
        note.key = "souvenirs:" + conv;
        (void)notifs::post(std::move(note));
    }
    // 1.21.0 : LE PONT DE L'AGENT, sur le fil de l'interface - les confirmations de scan demandees par l'IA s'ouvrent ici,
    // et le dernier compte rendu de scan est recopie pour resultat_detection.
    if (bridge_) {
        std::vector<agent::Bridge::ScanAsk> asks;
        {
            std::lock_guard<std::mutex> lk(bridge_->m);
            asks.swap(bridge_->scanAsks);
        }
        for (const auto& a : asks) {
            std::string why;
            if (!host_.askScan || !host_.askScan(a.encoded, a.reason, &why)) {
                notice_ = "Assistant IA : la confirmation de la d\xC3\xA9tection n'a pas pu s'ouvrir" + (why.empty() ? std::string(".") : " : " + why);
                ++revision_;
            } else {
                expectScan();   // 1.21.2 : sa fin ira dans l'onglet Notifications
            }
        }
        if (host_.scanSerial) {
            const std::uint64_t serial = host_.scanSerial();
            bool fresh = false;
            {
                std::lock_guard<std::mutex> lk(bridge_->m);
                fresh = serial != 0 && serial != bridge_->scanSerial;
            }
            if (fresh) {
                std::string text = host_.scanReportText ? host_.scanReportText() : std::string{};
                // 1.21.2 : le scan que l'IA a prepare (la confirmation ouverte par elle, ou << Detecter ce qui repond... >>)
                // vient de finir - l'onglet Notifications le dit, avec la premiere ligne du compte rendu.
                if (scanAwaited_ >= 0.0 && lastNow_ - scanAwaited_ < kScanAwaitSeconds) {
                    scanAwaited_ = -1.0;
                    std::string head = text.substr(0, text.find('\n'));
                    if (!head.empty() && head.back() == '.') head.pop_back();
                    notifs::Item note;
                    note.category = "R\xC3\xA9seau";
                    note.title = "Scan r\xC3\xA9seau termin\xC3\xA9" + (head.empty() ? std::string{} : " : " + notifs::oneLine(head, 120));
                    note.detail = "Le compte rendu est dans Communication \xE2\x80\xBA Scanner IP ; l'IA peut le lire (r\xC3\xA9sultat de la "
                                  "d\xC3\xA9tection). Rien n'est ajout\xC3\xA9 au projet sans ton accord.";
                    note.tone = "ok";
                    note.button = "Voir le compte rendu";
                    note.action = notifs::toScanReport();
                    note.key = "scan:" + std::to_string(serial);
                    (void)notifs::post(std::move(note));
                }
                std::lock_guard<std::mutex> lk(bridge_->m);
                bridge_->scanSerial = serial;
                bridge_->scanText = std::move(text);
            }
        }
    }
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
    missionsTick();   // 1.22.0 : la reprise au demarrage, le projet change (Ctrl+Z...), les audits finis
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
    // 1.21.0 : une conversation d'avant la 1.21 : ses pieces jointes recoivent leur conversation, leur message, leur type ;
    // celles dont le texte extrait n'est plus dans le cache (ou jamais analysees) repartent a l'analyse.
    Conversation& c = conversations_[static_cast<std::size_t>(i)];
    bool changed = attach::adopt(c) > 0;
    const auto ctx = attach::buildContext(c);
    for (const auto& f : ctx.files) {
        if (f.attachment.removed || f.availability == attach::Availability::Missing) continue;
        if (!f.textMissing && !f.attachment.analysis.empty()) continue;
        for (auto& m : c.messages)
            for (auto& a : m.attachments)
                if (a.id == f.attachment.id && a.analysis != "en-cours") {
                    submitAnalysis(a);
                    changed = true;
                }
    }
    // 1.21.0 : ses classeurs deja analyses sont relus sur un fil (l'IA integree, la fenetre << Modele... >> s'en servent)
    for (const auto& f : ctx.files)
        if (!f.attachment.removed && f.availability != attach::Availability::Missing && f.attachment.analysis == "analysee"
            && sheets::workbookExt(f.attachment.ext))
            requestWorkbook(f.attachment);
    if (changed) save(c);
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
                // 1.21.0 : un classeur analyse part lu (son texte extrait, ou sa lecture compacte et l'outil des lignes)
                if (sheets::workbookExt(p.attachment.ext) && p.attachment.analysis == "analysee") continue;
                ProviderAttachment pa{p.attachment.name, mediaTypeOf(p.attachment.name), attachmentPath(p.attachment), p.attachment.bytes, false, {}};
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
            if ((!host_.readPlc || !isPlcExport(att.name)) && !sheets::workbookExt(att.ext))   // 1.21.0 : un classeur, elle le lit
                files += (files.empty() ? "" : ", ") + std::string("**") + att.name + "**";
        if (!files.empty())
            reply.text = "J'ai bien re\xC3\xA7u " + files + ", mais l'IA int\xC3\xA9gr\xC3\xA9" "e ne lit pas le contenu de ces pi\xC3\xA8" "ces jointes "
                         "(seulement les classeurs .xlsx/.xlsm et l'export de l'automate, .XPG).\n\n" + reply.text;
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
    // 1.21.0 : les classeurs joints (lus, reconnus) - l'IA integree en fait des propositions
    in.workbooks = attachedWorkbooks(c ? c : current(), true);
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
    const MissionCall call = gen_.mission;   // 1.22.0 : l'appel de mission arrete (marque interrompu ci-dessous)
    Conversation* c = generatingConversation();
    if (c && gen_.message >= 0 && gen_.message < static_cast<int>(c->messages.size())) {
        Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
        const std::string shown = shownText();
        m.text = shown.empty() ? std::string(kStoppedEmpty) : shown + " \xE2\x80\xA6";
        m.state = "arrete";
        m.date = isoDate(now());
        m.provider = gen_.provider;
        m.model = gen_.model;
        if (!call.id.empty()) m.extra.set("mission", call.id);
        gen_ = {};
        save(*c);
    } else {
        gen_ = {};
    }
    notice_ = "G\xC3\xA9n\xC3\xA9ration arr\xC3\xAAt\xC3\xA9" "e : la r\xC3\xA9ponse est gard\xC3\xA9" "e telle quelle, sans proposition.";
    // 1.22.0 : une reponse de mission arretee - l'appel est marque interrompu, ses etapes redeviennent pretes ; rien
    // n'est propose ni rejoue.
    endMissionCall(call, true, "la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e avant d'avoir propos\xC3\xA9 quoi que ce soit");
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
    // 1.21.0 : sa conversation, son type ; son analyse part sur le fil d'analyse (le texte des PDF, DOCX, XLSX...).
    attach::adopt(a, cur() ? cur()->id : std::string{});
    submitAnalysis(a);
    Pending p;
    p.attachment = std::move(a);
    if (lastNow_ > 0) p.started = lastNow_;   // le traitement part de maintenant (sinon : au prochain tick)
    pending_.push_back(std::move(p));
    ++revision_;
    return true;
}

void Assistant::detach(std::size_t index) {
    if (index >= pending_.size()) return;
    if (analyzer_) analyzer_->cancel(pending_[index].attachment.id);   // 1.21.0 : son analyse n'est plus attendue
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
    return std::any_of(pending_.begin(), pending_.end(), [](const Pending& p) {
        // 1.21.0 : un classeur joint est lu et reconnu avant l'envoi (l'IA en a besoin) - c'est court
        return p.progress < 1.f || (p.attachment.analysis == "en-cours" && sheets::workbookExt(p.attachment.ext));
    });
}

// ---- 1.21.0 : le contexte de la discussion ----------------------------------------------------------------------------
void Assistant::submitAnalysis(Attachment& a) {
    if (!analyzer_) analyzer_ = std::make_unique<attach::Analyzer>();
    attach::markRunning(a);
    analyzer_->submit(a);
}

void Assistant::pollAnalyses() {
    if (!analyzer_) return;
    for (const auto& o : analyzer_->poll()) {
        // 1.21.0 : un classeur lu (garde pour l'IA) ; un modele de fichier reconnu compte une utilisation
        if (o.workbook) keepWorkbook(o.attachment, o.workbook);
        if (!o.model.empty()) (void)sheets::noteUse(o.model);
        bool done = false;
        for (auto& p : pending_)
            if (attach::applyOutcome(p.attachment, o)) done = true;
        const bool fresh = done;   // 1.21.2 : une piece qui vient d'etre jointe (pas une relecture d'une ancienne conversation)
        if (!done)
            for (auto& c : conversations_)
                if (attach::applyOutcome(c, o)) {
                    save(c);
                    done = true;
                    break;
                }
        if (done) ++revision_;
        // 1.21.2 : l'onglet Notifications - une piece qui ne se lit pas, un classeur qui vient d'etre joint, reconnu ou lu (son
        // modele, sa detection) ; un classeur d'une ancienne conversation relu n'est pas une nouvelle.
        const Attachment* a = done ? findAttachment(o.attachment) : nullptr;
        if (a && (o.analysis.state == "echec" || (o.workbook && fresh))) {
            notifs::Item note;
            note.key = "piece:" + o.attachment;
            if (o.analysis.state == "echec") {
                note.category = "Pi\xC3\xA8" "ce jointe";
                note.title = "Pi\xC3\xA8" "ce jointe illisible : " + guillemets(a->name);
                note.detail = (o.analysis.error.empty() ? std::string("son analyse a \xC3\xA9" "chou\xC3\xA9") : o.analysis.error)
                              + " ; l'IA ne la lit pas tant qu'elle n'est pas lisible.";
                note.tone = "error";
                note.button = "Voir la pi\xC3\xA8" "ce";
                note.action = notifs::toAttachment(o.attachment);
            } else {
                note.category = "Classeur";
                note.title = (o.model.empty() ? "Classeur lu : " : "Classeur reconnu : ") + guillemets(a->name);
                note.detail = notifs::oneLine(o.analysis.summary.empty() ? std::string("ses feuilles sont lues pour l'IA") : o.analysis.summary, 300);
                note.tone = o.model.empty() ? "info" : "ok";
                note.button = "Mod\xC3\xA8le\xE2\x80\xA6";
                note.action = notifs::toFileModel(o.attachment);
            }
            (void)notifs::post(std::move(note));
        }
    }
}

bool Assistant::analysing() const {
    return analyzer_ && (analyzer_->busy() || analyzer_->queued() > 0);
}

attach::DiscussionContext Assistant::discussionContext() const {
    std::vector<Attachment> waiting;
    waiting.reserve(pending_.size());
    for (const auto& p : pending_) waiting.push_back(p.attachment);
    if (const Conversation* c = current()) return attach::buildContext(*c, waiting);
    return attach::buildContext(Conversation{}, waiting);
}

// ---- 1.21.0 : les classeurs joints ----------------------------------------------------------------------------------------
void Assistant::keepWorkbook(const std::string& id, std::shared_ptr<const sheets::Workbook> wb) const {
    if (id.empty() || !wb) return;
    workbooks_[id] = std::move(wb);
    // borne : huit classeurs (un classeur de 20 000 lignes pese quelques Mo) ; les plus anciens partent (l'identifiant d'une
    // piece porte sa date), jamais celui-ci ni ceux du prochain message
    while (workbooks_.size() > 8) {
        const auto victim = std::find_if(workbooks_.begin(), workbooks_.end(), [&](const auto& kv) {
            return kv.first != id && std::none_of(pending_.begin(), pending_.end(), [&](const Pending& p) { return p.attachment.id == kv.first; });
        });
        if (victim == workbooks_.end()) break;
        workbooks_.erase(victim);
    }
}

void Assistant::requestWorkbook(const Attachment& a) const {
    if (a.id.empty() || workbooks_.count(a.id)) return;
    for (const auto& l : loads_)
        if (l && l->id == a.id) return;
    const std::string path = attachmentPath(a).empty() ? a.source : attachmentPath(a);
    if (path.empty()) return;
    auto job = std::make_shared<WorkbookLoad>();
    job->id = a.id;
    WorkbookLoad* raw = job.get();
    job->worker = std::thread([raw, path] {
        auto wb = std::make_shared<sheets::Workbook>();
        std::string why;
        if (sheets::readWorkbookFile(path, *wb, &why)) raw->result = std::move(wb);
        raw->finished = true;
    });
    loads_.push_back(std::move(job));
}

void Assistant::pollWorkbooks() {
    for (auto it = loads_.begin(); it != loads_.end();) {
        auto& l = *it;
        if (!l || !l->finished) {
            ++it;
            continue;
        }
        if (l->worker.joinable()) l->worker.join();
        if (l->result) {
            keepWorkbook(l->id, std::move(l->result));
            ++revision_;
        }
        it = loads_.erase(it);
    }
}

bool Assistant::waitWorkbooks(int ms) {
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    for (;;) {
        const bool busy = std::any_of(loads_.begin(), loads_.end(), [](const auto& l) { return l && !l->finished; })
                          || (analyzer_ && analyzer_->busy());
        if (!busy) break;
        if (std::chrono::steady_clock::now() >= until) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    pollAnalyses();
    pollWorkbooks();
    return true;
}

std::shared_ptr<const sheets::Workbook> Assistant::workbookOf(const std::string& attachmentId) const {
    if (const auto it = workbooks_.find(attachmentId); it != workbooks_.end()) return it->second;
    if (const Attachment* a = attachmentById(attachmentId); a && a->analysis != "en-cours" && sheets::workbookExt(a->ext)) requestWorkbook(*a);
    return nullptr;
}

bool Assistant::postFileModel(const sheets::FileModel& m, bool created) {
    if (!cur()) newConversation();
    Conversation* c = cur();
    if (!c) return false;
    Message a;
    a.id = "m" + std::to_string(c->messages.size() + 1);
    a.role = "assistant";
    a.date = isoDate(now());
    a.text = sheets::chatText(m, created);
    a.state = "fini";
    // ecrit par le LOGICIEL, pas par le modele : jamais renvoye au fournisseur comme une de ses reponses
    a.extra = json::Value::object();
    a.extra.set("logiciel", std::string("modele-fichier"));
    c->messages.push_back(std::move(a));
    if (Conversation* moved = touch(*c)) save(*moved);
    ++revision_;
    return true;
}

const Attachment* Assistant::attachmentById(const std::string& attachmentId) const {
    for (const auto& p : pending_)
        if (p.attachment.id == attachmentId) return &p.attachment;
    if (const Conversation* c = current())
        for (const auto& m : c->messages)
            for (const auto& a : m.attachments)
                if (a.id == attachmentId) return &a;
    return nullptr;
}

// 1.21.2 : dans toutes les conversations (une notification peut viser une piece d'une conversation fermee).
const Attachment* Assistant::findAttachment(const std::string& attachmentId, std::string* conversation) const {
    if (conversation) conversation->clear();
    if (attachmentId.empty()) return nullptr;
    for (const auto& p : pending_)
        if (p.attachment.id == attachmentId) {
            if (conversation && current()) *conversation = current()->id;
            return &p.attachment;
        }
    for (const auto& c : conversations_)
        for (const auto& m : c.messages)
            for (const auto& a : m.attachments)
                if (a.id == attachmentId) {
                    if (conversation) *conversation = c.id;
                    return &a;
                }
    return nullptr;
}

void Assistant::expectScan() { scanAwaited_ = std::max(0.0, lastNow_); }

Attachment* Assistant::attachmentMut(const std::string& attachmentId, Conversation** owner) {
    if (owner) *owner = nullptr;
    for (auto& p : pending_)
        if (p.attachment.id == attachmentId) return &p.attachment;
    if (Conversation* c = cur())
        for (auto& m : c->messages)
            for (auto& a : m.attachments)
                if (a.id == attachmentId) {
                    if (owner) *owner = c;
                    return &a;
                }
    return nullptr;
}

bool Assistant::setWorkbookChoice(const std::string& attachmentId, const sheets::Choice& choice, std::string* why) {
    Conversation* owner = nullptr;
    Attachment* a = attachmentMut(attachmentId, &owner);
    if (!a) {
        if (why) *why = "pi\xC3\xA8" "ce jointe introuvable dans cette discussion";
        return false;
    }
    if (choice.layout.sheet.empty() || choice.layout.columns.empty()) {
        if (why) *why = "aucune colonne choisie";
        return false;
    }
    sheets::setChoice(*a, choice);
    if (!choice.modelId.empty()) (void)sheets::noteUse(choice.modelId);
    if (owner) save(*owner);
    appendActivity("fichier", "Classeur " + a->name + " : " + (choice.modelName.empty() ? std::string("correspondance choisie") : "mod\xC3\xA8le " + choice.modelName) + " ("
                                  + sheets::genreLabel(choice.layout.genre) + ")");
    ++revision_;
    return true;
}

bool Assistant::clearWorkbookChoice(const std::string& attachmentId, std::string* why) {
    Conversation* owner = nullptr;
    Attachment* a = attachmentMut(attachmentId, &owner);
    if (!a) {
        if (why) *why = "pi\xC3\xA8" "ce jointe introuvable dans cette discussion";
        return false;
    }
    sheets::clearChoice(*a);
    if (owner) save(*owner);
    ++revision_;
    return true;
}

std::vector<sheets::Attached> Assistant::attachedWorkbooks(const Conversation* c, bool withPending) const {
    std::vector<sheets::Attached> out;
    const auto add = [&](const Attachment& a) {
        if (a.removed || !sheets::workbookExt(a.ext.empty() ? std::string_view(a.type) : std::string_view(a.ext))) return;
        if (std::any_of(out.begin(), out.end(), [&](const sheets::Attached& x) { return x.id == a.id; })) return;
        std::string path = attachmentPath(a);
        if (path.empty()) path = a.source;
        std::shared_ptr<const sheets::Workbook> wb;
        if (const auto it = workbooks_.find(a.id); it != workbooks_.end()) wb = it->second;
        else if (a.analysis == "analysee") requestWorkbook(a);
        out.push_back(sheets::attachedOf(a, std::move(path), std::move(wb)));
    };
    if (c)
        for (auto m = c->messages.rbegin(); m != c->messages.rend(); ++m)   // le plus recent d'abord
            for (const auto& a : m->attachments) add(a);
    if (withPending)
        for (const auto& p : pending_) add(p.attachment);
    return out;
}

std::vector<sheets::Attached> Assistant::workbooks() const { return attachedWorkbooks(current(), true); }

bool Assistant::removeFromDiscussion(const std::string& attachmentId, std::string* why) {
    for (std::size_t i = 0; i < pending_.size(); ++i)
        if (pending_[i].attachment.id == attachmentId) {
            detach(i);   // pas encore envoyee : elle part simplement
            return true;
        }
    Conversation* c = cur();
    if (!c) {
        if (why) *why = "aucune conversation ouverte";
        return false;
    }
    if (!attach::removeFromDiscussion(*c, attachmentId, false, why)) return false;
    if (analyzer_) analyzer_->cancel(attachmentId);
    save(*c);
    ++revision_;
    return true;
}

bool Assistant::restoreToDiscussion(const std::string& attachmentId, std::string* why) {
    Conversation* c = cur();
    if (!c) {
        if (why) *why = "aucune conversation ouverte";
        return false;
    }
    if (!attach::restoreToDiscussion(*c, attachmentId, why)) return false;
    save(*c);
    ++revision_;
    return true;
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
    // 1.22.0 : une proposition de mission ne s'applique pas si la mission est en pause, annulee, ou si la proposition
    // n'est plus la bonne (obsolete, annulee) ; ni sur un autre projet que celui de la mission.
    if (std::string w; !missionMayApply(p->id, &w)) {
        if (why) *why = w;
        notice_ = "Assistant IA : " + w;
        ++revision_;
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
        notifyApplyFailed(c->id, message, *p, w);   // 1.21.2 : l'onglet Notifications
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
    std::optional<plan::Report> real;   // 1.22.0 : la mission le recoit
    if (const auto after = host_.document ? host_.document() : nullptr; after && before) {
        const plan::Report rep = plan::report(*before, after->project, pl, prop);
        prop.plan = plan::toJson(pl);
        prop.report = plan::reportToJson(rep);
        if (pl.worthShowing()) {
            appendActivity("conversation", "Plan appliqu\xC3\xA9 : " + rep.verdict);
            for (const auto& f : rep.failures) appendActivity("conversation", "\xC3\x89tape non appliqu\xC3\xA9" "e : " + f);
        }
        real = rep;
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
    // 1.22.0 : UNE PROPOSITION DE MISSION - son resultat REEL (le compte rendu du projet d'avant et d'apres) passe a la
    // mission : ses etapes terminees, en erreur ; l'empreinte du projet ; les notifications de la mission (a la place de
    // celle d'une proposition ordinaire).
    const bool fromMission = missionApplied(prop, pl, real ? &*real : nullptr);
    // 1.21.2 : l'onglet Notifications - appliquee (ce qu'elle a cree, ce qui est laisse de cote), et sa carte.
    if (!fromMission) {
        notifs::Item note;
        note.category = "Proposition";
        note.title = "Proposition appliqu\xC3\xA9" "e" + (prop.summary.empty() ? std::string{} : " : " + notifs::oneLine(prop.summary, 90));
        const std::string made = creationSummary(prop);
        note.detail = skipped.empty()
                          ? (made.empty() ? std::string("Ctrl+Z la reprend d'un coup.") : "Cr\xC3\xA9\xC3\xA9" "s : " + made + ". Ctrl+Z reprend tout d'un coup.")
                          : plural(skipped.size(), "erreur laiss\xC3\xA9" "e de c\xC3\xB4t\xC3\xA9", "erreurs laiss\xC3\xA9" "es de c\xC3\xB4t\xC3\xA9")
                                + " (sous la r\xC3\xA9ponse et dans le journal d'activit\xC3\xA9).";
        note.tone = skipped.empty() ? "ok" : "warning";
        note.button = "Voir la carte";
        note.action = notifs::toCard(c->id, message);
        note.key = "proposition:" + prop.id;
        (void)notifs::post(std::move(note));
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
    if (std::string w; !missionMayApply(p->id, &w)) return fail(w);   // 1.22.0 : pause, annulee, obsolete, autre projet
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
    const Proposal& jobProp = *c->messages[static_cast<std::size_t>(job->message)].proposal;   // 1.21.2 : les notifications
    // 1.22.0 : la mission a ete mise en pause ou annulee pendant l'application de fond : rien n'est pose.
    if (std::string w; !missionMayApply(jobProp.id, &w)) {
        notice_ = "Assistant IA : " + w + " Rien n'est appliqu\xC3\xA9.";
        notifyApplyFailed(c->id, job->message, jobProp, w);
        return;
    }
    if (!doc || doc->revision != job->revision) {
        notice_ = "Assistant IA : le projet a chang\xC3\xA9 pendant l'application en arri\xC3\xA8re-plan : rien n'est appliqu\xC3\xA9 "
                  "(Appliquer la refait sur le projet tel qu'il est).";
        appendActivity("conversation", "Proposition non appliqu\xC3\xA9" "e : le projet a chang\xC3\xA9 pendant son application en arri\xC3\xA8re-plan.");
        notifyApplyFailed(c->id, job->message, jobProp,
                          "Le projet a chang\xC3\xA9 pendant l'application en arri\xC3\xA8re-plan (Appliquer la refait sur le projet tel qu'il est).");
        // 1.22.0 : une proposition de mission preparee sur un projet qui a change est OBSOLETE : ses etapes redeviennent
        // pretes, la prochaine proposition partira du projet tel qu'il est (rien n'est ecrase).
        missionDropped(jobProp.id, "obsolete", "le projet a chang\xC3\xA9 pendant son application en arri\xC3\xA8re-plan : rien n'a \xC3\xA9t\xC3\xA9 appliqu\xC3\xA9");
        return;
    }
    if (!job->result) {
        notice_ = job->why.empty() ? std::string("La proposition ne s'applique pas.") : job->why;
        appendActivity("conversation", "Proposition non appliqu\xC3\xA9" "e : " + notice_);
        notifyApplyFailed(c->id, job->message, jobProp, notice_);
        return;
    }
    std::string w;
    auto cmd = commandSetting(doc, jobProp, job->result, &w);
    if (!cmd) {
        notice_ = w;
        notifyApplyFailed(c->id, job->message, jobProp, w);
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
    // 1.22.0 : une proposition de mission refusee est sortie de la mission (ses etapes sont redevenues pretes) : elle ne
    // revient pas en douce - la suite de la mission repropose ces etapes sur le projet tel qu'il est.
    if (const mission::Mission* ms = missionOfProposal(p->id)) {
        notice_ = "Assistant IA : cette proposition de la mission " + guillemets(ms->name)
                  + " a \xC3\xA9t\xC3\xA9 refus\xC3\xA9" "e ; demande la suite de la mission pour que ses \xC3\xA9tapes soient repropos\xC3\xA9" "es.";
        ++revision_;
        return false;
    }
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
    // 1.22.0 : une proposition de mission refusee - ses etapes redeviennent pretes (la mission le retient).
    missionDropped(p->id, "refusee", "refus\xC3\xA9" "e par l'utilisateur : le projet n'a pas chang\xC3\xA9");
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
    // 1.22.0 : le plan d'une mission (planifier_mission) - enregistre (le nombre d'etapes), ou l'erreur exacte.
    if (m.tool->name == mission::kPlanTool) {
        if (!m.tool->error.empty()) return {"Erreur : le plan n'est pas enregistr\xC3\xA9 : " + m.tool->error, true};
        const std::int64_t n = m.extra.integer("planEtapes");
        return {"plan enregistr\xC3\xA9 : " + plural(static_cast<std::size_t>(std::max<std::int64_t>(0, n)), "\xC3\xA9tape", "\xC3\xA9tapes")
                    + ". Rien n'est encore modifi\xC3\xA9 dans le projet : n'appelle pas proposer_operations avant que l'utilisateur demande la "
                      "suite de la mission.",
                false};
    }
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
    r.maxTokens = outputTokens();   // 1.21.3 (16000 avant)
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
                // 1.21.0 : l'original quand le modele le lit, sinon le texte extrait ; une piece retiree ne part plus.
                for (const auto& a : m.attachments) {
                    // 1.21.0 : UN CLASSEUR STRUCTURE (un modele de fichier reconnu, une detection, un choix) part en lecture
                    // compacte - sa mise en page, ses premieres lignes ; toutes ses lignes se lisent avec l'outil
                    // lignes_classeur (offert avec l'appel d'outil) : le texte brut n'est pas envoye.
                    if (!toolsInText() && !a.removed && sheets::workbookExt(a.ext))
                        if (sheets::choiceOf(a)) {
                            std::shared_ptr<const sheets::Workbook> wb;
                            if (const auto it = workbooks_.find(a.id); it != workbooks_.end()) wb = it->second;
                            ProviderAttachment pa;
                            pa.name = a.name + " (lignes lues).txt";
                            pa.mediaType = "text/plain";
                            pa.prepared = true;
                            pa.blocks.push_back(sheets::compactBlock(sheets::attachedOf(a, attachmentPath(a), std::move(wb))));
                            pa.bytes = pa.blocks.front().size();
                            t.attachments.push_back(std::move(pa));
                            continue;
                        }
                    if (auto pa = attach::forProvider(a, settings_.provider == "anthropic")) t.attachments.push_back(std::move(*pa));
                }
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
            // Une erreur du logiciel n'est pas un tour du modele ; un arret sans texte non plus ; 1.21.2 : un message ecrit
            // par le logiciel (un modele de fichier enregistre) non plus.
            if (m.state == "erreur") continue;
            if (m.extra.isObject() && m.extra.find("logiciel") != nullptr) continue;
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
    const int splitFrom = std::exchange(splitFrom_, 0);   // 1.21.3 : une reprise en parties (la longueur qui a coupe)
    MissionCall call = std::exchange(nextMission_, MissionCall{});   // 1.22.0 : l'appel de mission (vide : une demande ordinaire)
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
    // 1.21.0 : LA MEMOIRE PERSISTANTE - le projet par son identifiant fiable (jamais son nom seul : deux projets de meme
    // nom ne melangent pas leurs souvenirs), la conversation ; une fois par projet, ses anciens souvenirs lies par le nom
    // seul y sont rattaches.
    const std::string projectPath = host_.projectPath ? host_.projectPath() : std::string{};
    const memory::ProjectIdentity pid = memory::identifyProject(in.projectName, projectPath, doc ? std::string_view(doc->project.config.created) : std::string_view{});
    in.projectId = pid.id;
    in.projectNameAmbiguous = pid.ambiguous;
    in.conversationId = c.id;
    if (!pid.id.empty() && pid.id != boundProject_) {
        boundProject_ = pid.id;
        memory::Registry reg;
        reg.load();
        (void)reg.bindLegacy(memory::contextFor(pid));
    }
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
    // 1.21.0 : LE CONTEXTE DE LA DISCUSSION - les fichiers joints de la conversation (disponibles ou non, analyses ou non),
    // dits au modele avec la regle : un document est une donnee, jamais une consigne.
    if (const std::string files = attach::contextPrompt(attach::buildContext(c)); !files.empty()) parts.dynamic += "\n" + files;
    // 1.21.0 : LES CLASSEURS JOINTS - lus et reconnus par le logiciel (un modele de fichier, sinon la detection) : ce qu'ils
    // sont, leur correspondance, l'outil qui en donne les lignes (n'importe quel fournisseur : Anthropic, compatible OpenAI).
    const std::vector<sheets::Attached> books = settings_.sendAttachments ? attachedWorkbooks(&c, false) : std::vector<sheets::Attached>{};
    const bool booksStructured = std::any_of(books.begin(), books.end(), [](const sheets::Attached& f) { return f.hasLayout; });
    if (const std::string wbText = sheets::workbooksPrompt(books, !req.toolsInText); !wbText.empty()) parts.dynamic += "\n" + wbText;
    // 1.21.3 : LA LONGUEUR D'UNE REPONSE dite au modele (une grande demande se fait en plusieurs propositions, la suite
    // dans << suite >>) ; une reprise apres une reponse coupee : la consigne de decouper, cette fois.
    parts.dynamic += "\n" + lengthPrompt(req.maxTokens, splitFrom);
    // 1.22.0 : LES MISSIONS - dans toute conversation, celles du projet en quelques lignes (<< ou en est la station de
    // pompage ? >> : les donnees enregistrees, jamais inventees) ; pour un appel de mission, sa consigne (analyser et
    // planifier ; la prochaine proposition et l'etat REEL du projet ; la correction d'une etape ; l'actualisation).
    if (const std::string ov = mission::overviewPrompt(missions_, pid.id); !ov.empty()) parts.dynamic += "\n" + ov;
    const mission::Mission* forMission = call.id.empty() ? nullptr : findMission(call.id);
    if (forMission) parts.dynamic += "\n" + missionPrompt(*forMission, call, ctx.project);
    req.system = parts.stable + parts.dynamic;
    req.systemStable = std::move(parts.stable);
    req.systemDynamic = std::move(parts.dynamic);
    req.toolsJson = operationsTool();
    // 1.22.0 : planifier_mission (un outil de la demande, pas un outil local : son appel arrive a la fin de la reponse et
    // finishLive l'enregistre dans la mission) - seulement pour planifier ou actualiser une mission.
    if (forMission && (call.purpose == "plan" || call.purpose == "actualisation"))
        req.toolsJson = textparts::withTool(req.toolsJson, mission::planToolJson());
    // 1.21.0 : LES OUTILS DE L'AGENT (AiAgentTools.hpp) - IHM Studio, avec l'appel d'outil : analyser_projet (le graphe,
    // en lecture seule) et executer_code (le bac a sable). Sur une COPIE du projet, prise ici sur le fil de l'interface : le
    // fil du fournisseur ne lit jamais le projet vivant ; ce qui modifie le projet reste une proposition a valider.
    if (core::hasIhm() && ctx.project && !req.toolsInText) {
        auto snap = std::make_shared<const hmi::Project>(*ctx.project);
        req.localTools.push_back(agent::analyseTool(snap));
        if (sandbox_) {
            // 1.21.2 : un essai fini du bac a sable va dans l'onglet Notifications (poste sur le fil du fournisseur : le
            // journal est sur entre fils) ; le bouton mene a la reponse qui l'a fait.
            const std::string convId = c.id;
            const int answer = static_cast<int>(c.messages.size());   // la reponse qui commence
            req.localTools.push_back(agent::sandboxTool(snap, sandboxProgram_, [convId, answer](const std::string& said, const std::string& tone) {
                notifs::Item note;
                note.category = "Bac \xC3\xA0 sable";
                note.title = notifs::oneLine(said, 140);
                note.detail = "Un essai de code de l'IA, sur une copie du projet : rien n'a chang\xC3\xA9 dans le projet. Le r\xC3\xA9sultat est dans sa r\xC3\xA9ponse.";
                note.tone = tone;
                note.button = "Voir la r\xC3\xA9ponse";
                note.action = notifs::toConversation(convId, answer);
                (void)notifs::post(std::move(note));
            }));
        }
        // la detection reseau : proposee, jamais lancee par l'IA (la confirmation s'ouvre chez l'utilisateur)
        if (host_.askScan && bridge_) {
            {
                std::lock_guard<std::mutex> lk(bridge_->m);
                bridge_->scanAvailable = true;
            }
            req.localTools.push_back(agent::scanTool(bridge_));
            req.localTools.push_back(agent::scanResultTool(bridge_));
        }
    }
    // 1.21.0 : LA MEMOIRE - proposer_souvenir (une PROPOSITION, provisoire : elle ne part avec aucune demande tant que
    // l'utilisateur ne l'a pas validee), chercher_souvenirs, chercher_conversations. Repondus sur le fil du fournisseur,
    // chacun avec son propre registre relu du disque (le registre n'est pas partage entre les fils).
    if (!req.toolsInText) {
        const memory::Context here = memory::contextFor(pid, c.id);
        const std::string convId = c.id;
        const std::string msgId = "m" + std::to_string(c.messages.size() + 1);   // la reponse qui commence
        auto counter = proposedMemories_;
        LocalTool remember;
        remember.name = std::string(memory::kRememberTool);
        remember.definitionJson = memory::rememberToolJson();
        remember.maxCalls = 5;
        remember.run = [here, convId, msgId, counter](const std::string& input, const auto&, const auto&) {
            memory::Registry reg;
            reg.load();
            bool failed = false;
            std::string out = memory::runRememberTool(reg, input, here, convId, msgId, &failed);
            if (!failed) ++*counter;
            // 1.21.2 : une contradiction (la reponse de l'outil la dit en CONTREDIT, suivie des souvenirs contredits) - dans
            // l'onglet Notifications, sur ce fil (le journal est sur entre fils).
            if (!failed && out.find("CONTREDIT") != std::string::npos) {
                const auto at = out.find("\n- ");
                const std::string first = at == std::string::npos ? std::string{} : out.substr(at + 3, out.find('\n', at + 3) - (at + 3));
                notifs::Item note;
                note.category = "Souvenirs";
                note.title = "Un souvenir propos\xC3\xA9 contredit ce qui est retenu";
                note.detail = (first.empty() ? std::string{} : "Contredit : " + notifs::oneLine(first, 220) + ". ")
                              + "Les deux sont gard\xC3\xA9s : choisis lequel est juste.";
                note.tone = "warning";
                note.button = "R\xC3\xA9soudre le conflit";
                note.action = notifs::toSouvenirs("conflits");
                note.key = "souvenirs-conflit:" + convId;
                (void)notifs::post(std::move(note));
            }
            return out;
        };
        req.localTools.push_back(std::move(remember));
        LocalTool searchMemory;
        searchMemory.name = std::string(memory::kSearchMemoryTool);
        searchMemory.definitionJson = memory::searchMemoryToolJson();
        searchMemory.maxCalls = 6;
        searchMemory.run = [here](const std::string& input, const auto&, const auto&) {
            memory::Registry reg;
            reg.load();
            return memory::runSearchMemoryTool(reg, input, here);
        };
        req.localTools.push_back(std::move(searchMemory));
        LocalTool searchConversations;
        searchConversations.name = std::string(convindex::kSearchConversationsTool);
        searchConversations.definitionJson = convindex::searchConversationsToolJson();
        searchConversations.maxCalls = 6;
        searchConversations.run = [convId](const std::string& input, const auto&, const auto&) {
            return convindex::runSearchConversationsTool(input, convId);
        };
        req.localTools.push_back(std::move(searchConversations));
        // 1.22.0 : chercher_missions, etat_mission - les missions RELUES DU DISQUE sur le fil du fournisseur (jamais celles
        // de la seance, qui vivent sur le fil de l'interface) : ce qui est enregistre, jamais invente.
        LocalTool searchMissions;
        searchMissions.name = std::string(mission::kSearchTool);
        searchMissions.definitionJson = mission::searchToolJson();
        searchMissions.maxCalls = 6;
        searchMissions.run = [projectId = pid.id](const std::string& input, const auto& status, const auto&) {
            status("Recherche dans les missions enregistr\xC3\xA9" "es");
            const mission::Loaded all = mission::loadAll();
            return mission::runSearchTool(all.missions, input, projectId);
        };
        req.localTools.push_back(std::move(searchMissions));
        LocalTool missionState;
        missionState.name = std::string(mission::kDetailTool);
        missionState.definitionJson = mission::detailToolJson();
        missionState.maxCalls = 6;
        missionState.run = [](const std::string& input, const auto& status, const auto&) {
            status("Lecture d'une mission enregistr\xC3\xA9" "e");
            const mission::Loaded all = mission::loadAll();
            return mission::runDetailTool(all.missions, input);
        };
        req.localTools.push_back(std::move(missionState));
        // 1.21.0 : lignes_classeur - les lignes des classeurs joints, par pages, telles que le logiciel les a lues (sur le fil
        // du fournisseur : un classeur pas encore lu l'est depuis sa copie ; le projet n'est jamais touche).
        if (booksStructured) {
            LocalTool rows;
            rows.name = std::string(sheets::kRowsTool);
            rows.definitionJson = sheets::rowsToolJson();
            rows.maxCalls = 12;
            rows.run = [books](const std::string& input, const auto& status, const auto&) {
                status("Lecture des lignes d'un classeur joint");
                return sheets::runRowsTool(books, input);
            };
            req.localTools.push_back(std::move(rows));
        }
    }
    const int maxTokens = req.maxTokens;
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
    gen_.maxTokens = maxTokens;      // 1.21.3
    gen_.split = splitFrom > 0;
    gen_.mission = std::move(call);   // 1.22.0
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
                // 1.21.3 : la longueur de la reponse refusee par le service, redemandee avec ce qu'il accepte (la limite du
                // modele : retenue pour la seance).
                if (e.text == "longueur" || e.text == "longueur-modele") {
                    const int n = std::atoi(e.detail.c_str());
                    if (n > 0) {
                        gen_.maxTokens = n;
                        if (e.text == "longueur-modele") outputCaps_[capKey()] = n;
                        gen_.detail = "Le service accepte au plus " + groupedTokens(n) + " jetons par r\xC3\xA9ponse : demande renvoy\xC3\xA9" "e avec cette longueur\xE2\x80\xA6";
                        gen_.phase = Phase::Waiting;
                    }
                    ++revision_;
                    break;
                }
                if (e.text == "operations") gen_.proposing = true;   // 1.21.3 : une proposition s'ecrit
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
                if (gen_.textTool && gen_.text.find("```") != std::string::npos) {
                    gen_.phase = Phase::Proposing;
                    gen_.proposing = true;   // 1.21.3
                }
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
    const MissionCall call = gen_.mission;   // 1.22.0 : l'appel de mission de cette reponse (vide : aucun)
    Conversation* c = generatingConversation();
    if (!c || gen_.message < 0 || gen_.message >= static_cast<int>(c->messages.size())) {
        gen_ = {};
        endMissionCall(call, true, "la conversation de la r\xC3\xA9ponse n'existe plus");
        ++revision_;
        return;
    }
    if (stopReason == "arrete") {   // arrete ailleurs (le fournisseur a ete coupe) : comme Arreter
        stop();
        return;
    }
    // 1.21.3 : UNE PROPOSITION COUPEE PAR LA LONGUEUR (max_tokens pendant qu'elle s'ecrivait : rien n'est propose) - redemandee
    // AUSSITOT EN PLUSIEURS PARTIES : la reponse coupee est remplacee par une nouvelle, avec la consigne de proposer une
    // premiere partie complete et de dire le reste dans << suite >>. Une fois seulement (une reprise coupee a son tour : dit).
    if (stopReason == "max_tokens" && gen_.proposing && !gen_.tool && !gen_.split && gen_.message + 1 == static_cast<int>(c->messages.size())) {
        const std::string request = gen_.request;
        const int cut = gen_.maxTokens > 0 ? gen_.maxTokens : outputTokens();
        ProviderUsage spent = gen_.carried;
        spent.input += gen_.usage.input + gen_.preread.input;
        spent.output += gen_.usage.output + gen_.preread.output;
        spent.cacheRead += gen_.usage.cacheRead + gen_.preread.cacheRead;
        spent.cacheWrite += gen_.usage.cacheWrite + gen_.preread.cacheWrite;
        const std::string cutText = trim(gen_.text);
        c->messages.pop_back();
        gen_ = {};
        splitFrom_ = cut;
        nextMission_ = call;   // 1.22.0 : la reprise en parties reste l'appel de la mission (ses etapes, son lot)
        std::string why;
        if (startLive(*c, request, &why)) {
            gen_.carried = spent;
            gen_.detail = "La proposition d\xC3\xA9passait la longueur d'une r\xC3\xA9ponse (" + groupedTokens(cut)
                          + " jetons) : redemand\xC3\xA9" "e en plusieurs parties\xE2\x80\xA6";
            notice_ = "Assistant IA : la proposition \xC3\xA9tait trop longue pour une r\xC3\xA9ponse ; elle est redemand\xC3\xA9" "e en plusieurs parties.";
            ++revision_;
            return;
        }
        // La reprise n'a pas pu partir : la reponse coupee revient, avec la raison.
        splitFrom_ = 0;
        Message back;
        back.id = "m" + std::to_string(c->messages.size() + 1);
        back.role = "assistant";
        back.text = cutText;
        back.state = "fini";
        back.date = isoDate(now());
        back.note = "La proposition d\xC3\xA9passait la longueur d'une r\xC3\xA9ponse (" + groupedTokens(cut) + " jetons) et la reprise en "
                    "plusieurs parties n'a pas pu partir : " + why;
        if (!call.id.empty()) back.extra.set("mission", call.id);
        c->messages.push_back(std::move(back));
        if ((c = touch(*c)) != nullptr) save(*c);
        endMissionCall(call, false, "la proposition d\xC3\xA9passait la longueur d'une r\xC3\xA9ponse et sa reprise en plusieurs parties n'a pas pu partir : " + why);
        ++revision_;
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
    if (!gen_.usage.empty() || !gen_.preread.empty() || !gen_.carried.empty())
        m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total() + gen_.carried.total()));
    if (gen_.textTool && gen_.tool) {
        std::string rest;
        if (extractProposalBlock(m.text, nullptr, &rest)) m.text = rest;
    }
    if (stopReason == "refusal") {
        m.note = "La demande a \xC3\xA9t\xC3\xA9 d\xC3\xA9" "clin\xC3\xA9" "e par le mod\xC3\xA8le : rien n'est propos\xC3\xA9.";
    } else if (stopReason == "max_tokens") {
        // 1.21.3 : la longueur dite ; une reprise en parties coupee a son tour ; une reponse en texte seulement.
        const std::string limit = gen_.maxTokens > 0 ? " (" + groupedTokens(gen_.maxTokens) + " jetons)" : std::string{};
        if (gen_.split)
            m.note = "M\xC3\xAAme en plusieurs parties, la r\xC3\xA9ponse d\xC3\xA9passe la longueur d'une r\xC3\xA9ponse" + limit
                     + " : aucune op\xC3\xA9ration n'est propos\xC3\xA9" "e. Demande une partie \xC3\xA0 la fois (par exemple la vue d'abord, puis "
                       "les alarmes), ou augmente \xC2\xAB Longueur d'une r\xC3\xA9ponse \xC2\xBB (Param\xC3\xA8tres de l'Assistant IA).";
        else if (gen_.proposing)
            m.note = "La r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e (la longueur d'une r\xC3\xA9ponse" + limit + " est atteinte) : aucune op\xC3\xA9ration "
                     "n'est propos\xC3\xA9" "e. Demande une partie \xC3\xA0 la fois, ou augmente \xC2\xAB Longueur d'une r\xC3\xA9ponse \xC2\xBB (Param\xC3\xA8tres).";
        else
            m.note = "La r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e (la longueur d'une r\xC3\xA9ponse" + limit + " est atteinte). Demande une r\xC3\xA9ponse "
                     "plus courte, ou augmente \xC2\xAB Longueur d'une r\xC3\xA9ponse \xC2\xBB (Param\xC3\xA8tres de l'Assistant IA).";
    }
    // 1.22.0 : une demande de PLAN de mission (planifier, actualiser) attend planifier_mission, et rien d'autre.
    const bool planCall = !call.id.empty() && (call.purpose == "plan" || call.purpose == "actualisation");
    bool planned = false;
    if (gen_.tool) {
        m.tool = *gen_.tool;
        if (m.tool->name == mission::kPlanTool) {
            // 1.22.0 : LE PLAN DE LA MISSION - lu, controle (titres, dependances connues, aucun cycle), ordonne par ses
            // dependances et enregistre ; un plan revu garde les etapes deja travaillees (mission::planFromTool).
            std::string why;
            int steps = 0;
            if (!planCall)
                m.tool->error = "planifier_mission ne sert qu'\xC3\xA0 planifier une mission (Cr\xC3\xA9" "er une mission IA, Actualiser) : rien n'est enregistr\xC3\xA9";
            else if (!acceptPlan(call.id, m.tool->input, &why, &steps))
                m.tool->error = why.empty() ? std::string("plan illisible") : why;
            else {
                planned = true;
                m.extra.set("mission", call.id);
                m.extra.set("planEtapes", steps);
            }
        } else if (m.tool->name != kOperationsTool) {
            m.tool->error = "outil inconnu : " + m.tool->name + " (seul " + std::string(kOperationsTool) + " existe)";
        } else if (planCall) {
            // la mission attend d'abord son plan : une proposition avant le plan n'est pas montree (l'IA le saura)
            m.tool->error = "la mission attend d'abord son plan (planifier_mission) : aucune op\xC3\xA9ration n'est propos\xC3\xA9" "e avant le plan, "
                            "ni avant que l'utilisateur demande la suite";
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
        if (!m.tool->error.empty()) {
            if (m.tool->name == mission::kPlanTool)
                m.note = "Le plan de la mission n'a pas pu \xC3\xAAtre enregistr\xC3\xA9 : " + m.tool->error + " (l'IA le saura \xC3\xA0 ta prochaine demande).";
            else if (planCall && m.tool->name == kOperationsTool)
                m.note = "Rien n'est propos\xC3\xA9 : " + m.tool->error + " (l'IA le saura \xC3\xA0 ta prochaine demande).";
            else
                m.note = "La proposition re\xC3\xA7ue est illisible, rien n'est propos\xC3\xA9 : " + m.tool->error + " (l'IA le saura \xC3\xA0 ta prochaine demande).";
        }
    }
    if (m.text.empty() && !m.proposal && m.note.empty()) m.note = "R\xC3\xA9ponse vide.";
    const int index = gen_.message;
    const bool open = current() && current()->id == c->id;
    const bool hasProposal = m.proposal.has_value();
    // 1.22.0 : la reponse d'un appel de mission - le plan enregistre (et dit en clair), ou la proposition liee a ses etapes
    // et verifiee ; sans proposition ni plan : l'appel est dit interrompu ou en echec, ses etapes redeviennent pretes.
    // (Peut ajouter un message du logiciel a la conversation : `m` n'est plus utilise apres.)
    if (!call.id.empty()) missionAnswered(call, *c, index, planned, stopReason);
    gen_ = {};
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    if (hasProposal && open && directMode()) autoApply(index);
}

void Assistant::failLive(const ProviderEvent& e) {
    retire();
    recordLive();
    const MissionCall call = gen_.mission;   // 1.22.0
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
        // 1.21.2 : l'onglet Notifications - l'erreur du service (cle refusee, limite, reseau...), meme repetee : une ligne
        // par fournisseur et par code (<< x3 >>) ; le bouton mene au message (Reessayer y est).
        {
            notifs::Item note;
            note.category = "Service";
            note.title = serviceErrorTitle(e.httpStatus);
            note.detail = guillemets(c->title) + " : " + notifs::oneLine(m.note, 300);
            note.tone = "error";
            note.button = "Voir le message";
            note.action = notifs::toConversation(c->id, gen_.message);
            note.key = "service:" + gen_.provider + ":" + std::to_string(e.httpStatus);
            (void)notifs::post(std::move(note));
        }
        if (!call.id.empty()) m.extra.set("mission", call.id);
        gen_ = {};
        if ((c = touch(*c)) != nullptr) save(*c);
    } else {
        gen_ = {};
    }
    notice_ = "Assistant IA : " + (e.text.empty() ? std::string("erreur") : e.text);
    // 1.22.0 : l'appel de mission en echec - l'erreur exacte dans l'historique, ses etapes redeviennent pretes (Reessayer
    // la redemande comme une demande de mission, sur l'etat reel du projet).
    endMissionCall(call, false, "l'appel \xC3\xA0 l'IA a \xC3\xA9" "chou\xC3\xA9 : " + (e.text.empty() ? std::string("erreur inconnue") : e.text));
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
    // 1.22.0 : la demande d'une mission (le plan, une proposition, une correction, une actualisation) - redemandee comme
    // une demande de mission (son etat REEL relu : le lot recalcule), jamais rejouee telle quelle.
    if (const Message& asked = c.messages.back(); asked.extra.isObject() && asked.extra.find("mission") != nullptr) {
        const Message user = asked;
        const std::string convId = c.id;
        c.messages.pop_back();
        if (relaunchMission(user, &w)) {
            suggestionsAfter_ = false;
            ++revision_;
            return true;
        }
        if (const int i = indexOf(convId); i >= 0) {
            auto& back = conversations_[static_cast<std::size_t>(i)];
            back.messages.push_back(user);
            back.messages.push_back(last);
        }
        return fail(w);
    }
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

std::string Assistant::capKey() const { return settings_.provider + "|" + trim(settings_.endpoint) + "|" + trim(settings_.model); }

int Assistant::outputTokens() const {
    int n = settings_.outputTokens > 0 ? std::max(settings_.outputTokens, kMinOutputTokens) : kAutoOutputTokens;
    if (settings_.outputTokens <= 0)
        if (const int window = contextWindow(); window > 0) n = std::max(kMinOutputTokens, std::min(n, window / 2));
    if (const auto it = outputCaps_.find(capKey()); it != outputCaps_.end() && it->second > 0) n = std::min(n, it->second);
    return n;
}

std::string Assistant::nextRequest(int message) const {
    const Proposal* p = proposal(message);
    if (!p || p->next.empty() || p->state != "appliquee") return {};
    return "Propose la suite : " + p->next;
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

// =====================================================================================================================
//  1.22.0 : LES MISSIONS IA PERSISTANTES (AiMission.hpp, AiMissionStore.hpp)
// ---------------------------------------------------------------------------------------------------------------------
//  Le fil de l'interface est le seul a toucher les missions de la seance (missions_) : chaque evenement (creation,
//  plan, proposition, validation, application, etape, erreur, decision, pause, reprise, annulation, audit) les
//  ENREGISTRE AUSSITOT (saveMission : ecriture atomique, avec .bak). Un appel a l'IA est ecrit AVANT de partir
//  (Mission::pending) et efface a sa fin : apres un arret brutal, il est retrouve et marque interrompu (rien n'est
//  rejoue). Le projet reel fait foi : une etape n'est terminee que si le compte rendu du projet d'avant et d'apres le
//  dit (onApplied), et chaque changement du projet (Ctrl+Z, Ctrl+Y, une suppression a la main) est rapproche des
//  missions au tick (verifyAgainst) - seulement quand Document::revision change, jamais a chaque image.
// =====================================================================================================================
mission::Mission* Assistant::missionMut(std::string_view id) noexcept {
    for (auto& m : missions_)
        if (m.id == id) return &m;
    return nullptr;
}

const mission::Mission* Assistant::findMission(std::string_view id) const noexcept {
    for (const auto& m : missions_)
        if (m.id == id) return &m;
    return nullptr;
}

mission::Mission* Assistant::missionOfProposalMut(std::string_view proposalId) noexcept {
    if (proposalId.empty()) return nullptr;
    for (auto& m : missions_)
        if (m.proposal(proposalId)) return &m;
    return nullptr;
}

const mission::Mission* Assistant::missionOfProposal(std::string_view proposalId) const noexcept {
    if (proposalId.empty()) return nullptr;
    for (const auto& m : missions_)
        if (m.proposal(proposalId)) return &m;
    return nullptr;
}

mission::Mission* Assistant::editableMission(std::string_view id, std::string* why) {
    mission::Mission* m = missionMut(id);
    if (!m) {
        if (why) *why = "Mission introuvable.";
        return nullptr;
    }
    if (missionsReadOnly_.count(id)) {
        if (why)
            *why = "La mission " + guillemets(m->name) + " vient d'une version plus r\xC3\xA9" "cente du logiciel : elle se consulte ici, mais ne se "
                   "modifie pas (elle n'est jamais r\xC3\xA9\xC3\xA9" "crite par cette version).";
        return nullptr;
    }
    return m;
}

std::string Assistant::openProjectId() const {
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return {};
    const std::string name = host_.projectName ? host_.projectName() : std::string{};
    const std::string path = host_.projectPath ? host_.projectPath() : std::string{};
    const std::string key = name + "\n" + path + "\n" + doc->project.config.created;
    if (key != missionProjectKey_) {
        missionProjectKey_ = key;
        missionProjectId_ = memory::identifyProject(name, path, doc->project.config.created).id;
    }
    return missionProjectId_;
}

void Assistant::saveMission(mission::Mission& m) {
    m.modified = isoDate(now());
    m.summary = mission::summaryOf(m);
    // Une mission lue d'un format plus recent n'est jamais reecrite (elle reste telle quelle sur le disque).
    if (!missionsReadOnly_.count(m.id)) {
        std::string why;
        if (!mission::save(m, &why)) {
            notice_ = "Assistant IA : la mission " + guillemets(m.name) + " n'a pas pu \xC3\xAAtre enregistr\xC3\xA9" "e"
                      + (why.empty() ? std::string(".") : " (" + why + ").");
            appendActivity("mission", "Mission non enregistr\xC3\xA9" "e : " + m.name + (why.empty() ? std::string{} : " - " + why));
        }
    }
    ++missionsRev_;
    ++revision_;
}

// UNE NOTIFICATION DE MISSION (l'onglet Notifications) : une cle par mission et par genre - un nouvel evenement du meme
// genre remplace l'ancien (<< x3 >>) ; jamais une par petite operation. Le bouton ouvre le tableau de bord de la mission.
void Assistant::notifyMission(const mission::Mission& m, const std::string& kind, std::string title, std::string detail, std::string tone) {
    notifs::Item n;
    n.category = "Mission";
    n.title = notifs::oneLine(title, 140);
    n.detail = notifs::oneLine(detail, 300);
    n.tone = std::move(tone);
    n.button = "Voir la mission";
    n.action = notifs::toMission(m.id);
    n.key = "mission:" + m.id + ":" + kind;
    (void)notifs::post(std::move(n));
}

// L'etat global vient de changer (refreshState) : bloquee, terminee - dit une fois.
void Assistant::missionTransition(const mission::Mission& m, mission::State before) {
    if (m.state == before) return;
    if (m.state == mission::State::Blocked)
        notifyMission(m, "bloquee", "Mission bloqu\xC3\xA9" "e : " + m.name, "Plus rien n'est r\xC3\xA9" "alisable sans ta d\xC3\xA9" "cision. " + mission::nextAction(m), "error");
    else if (m.state == mission::State::Done)
        notifyMission(m, "terminee", "Mission termin\xC3\xA9" "e : " + m.name,
                      m.finalReport.isObject() ? mission::reportFromJson(m.finalReport).verdict : std::string("Toutes les \xC3\xA9tapes obligatoires sont termin\xC3\xA9" "es."),
                      "ok");
}

// La conversation ou la mission travaille : l'ouverte si elle est la sienne, sinon la plus recente des siennes (rouverte),
// sinon (supprimees) l'ouverte si elle est vide, ou une neuve.
Conversation* Assistant::missionConversation(mission::Mission& m) {
    const auto mine = [&m](const std::string& id) { return id == m.conversation || listed(m.conversations, id); };
    if (Conversation* c = cur(); c && mine(c->id)) return c;
    for (const auto& conv : conversations_)   // les plus recentes d'abord
        if (mine(conv.id)) {
            const std::string id = conv.id;
            (void)select(id);
            return cur();
        }
    newConversation();
    return cur();
}

// Pourquoi une mission ne peut pas appeler l'IA maintenant (vide : elle le peut).
std::string Assistant::callBlocker(const mission::Mission& m) const {
    if (m.state == mission::State::Cancelled) return "La mission " + guillemets(m.name) + " est annul\xC3\xA9" "e.";
    if (m.state == mission::State::Paused) return "La mission " + guillemets(m.name) + " est en pause : reprends-la d'abord.";
    if (m.state == mission::State::Done) return "La mission " + guillemets(m.name) + " est termin\xC3\xA9" "e.";
    if (m.archived) return "La mission " + guillemets(m.name) + " est archiv\xC3\xA9" "e : sors-la des archives d'abord.";
    if (generating()) return "Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attends-la, ou arr\xC3\xAAte-la.";
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc)
        return "Aucun projet n'est ouvert : ouvre le projet de la mission (" + (m.project.name.empty() ? std::string("?") : m.project.name) + ") pour continuer.";
    if (!m.project.id.empty() && openProjectId() != m.project.id)
        return "Le projet ouvert n'est pas celui de la mission " + guillemets(m.name) + " (" + (m.project.name.empty() ? std::string("?") : m.project.name)
               + (m.project.path.empty() ? std::string{} : ", " + m.project.path) + ") : ouvre-le pour continuer. Rien n'est fait sur un autre projet.";
    return {};
}

// Remettre une mission au fait du projet ouvert avant un appel : un appel reste << en cours >> d'une autre seance (une
// fermeture brutale) est marque interrompu ; un projet qui etait introuvable et qui est de nouveau ouvert est reconnu.
void Assistant::settleStale(mission::Mission& m) {
    if (missionsReadOnly_.count(m.id)) return;
    if (generating() && gen_.mission.id == m.id) return;
    const auto doc = host_.document ? host_.document() : nullptr;
    const std::string pid = openProjectId();
    const bool stalePending = m.pending.active;
    const bool backAgain = m.coherence == mission::Coherence::Missing && doc && (m.project.id.empty() || m.project.id == pid);
    if (!stalePending && !backAgain) return;
    const mission::State before = m.state;
    (void)mission::recover(m, pid, doc ? &doc->project : nullptr, isoDate(now()));
    m.pending = {};
    missionsToRecover_.erase(std::remove(missionsToRecover_.begin(), missionsToRecover_.end(), m.id), missionsToRecover_.end());
    missionTransition(m, before);
    saveMission(m);
}

// LA DEMANDE D'UNE MISSION : un message de l'utilisateur dans la conversation de la mission (marque : la mission, le but),
// l'appel ECRIT AVANT DE PARTIR (pending, les etapes << en cours >>, l'empreinte du projet), puis startLive avec l'appel
// (ses consignes, ses outils). Rien ne change si la demande ne part pas.
bool Assistant::launchMission(mission::Mission& m, std::string text, MissionCall call, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!settings_.enabled) return fail("L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres de l'Assistant IA).");
    if (generating()) return fail("Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attends-la, ou arr\xC3\xAAte-la.");
    if (processing()) return fail("Les pi\xC3\xA8" "ces jointes sont en cours de traitement.");
    if (!live())
        return fail("Une mission travaille avec une IA connect\xC3\xA9" "e (Anthropic, ou un service compatible OpenAI) : choisis-la dans les "
                    "Param\xC3\xA8tres de l'Assistant IA.");
    std::string w;
    if (!ready(&w)) return fail(w);
    if (budgetBlocks(&w)) return fail(w);
    const std::string id = m.id;
    call.id = id;
    Conversation* cp = missionConversation(m);
    if (!cp) return fail("Aucune conversation pour la mission.");
    overBudgetOk_ = false;
    localSuggestions_.clear();
    Conversation& c = *cp;
    const Conversation before = c;
    const mission::Mission saved = m;   // tout revient si l'appel ne part pas
    const std::string when = isoDate(now());
    Message u;
    u.id = "m" + std::to_string(c.messages.size() + 1);
    u.role = "utilisateur";
    u.date = when;
    u.text = text;
    for (const auto& p : pending_) u.attachments.push_back(p.attachment);   // les pieces jointes attendues partent avec
    u.extra = json::Value::object();
    u.extra.set("mission", id);
    u.extra.set("appelMission", call.purpose);
    if (!call.step.empty()) u.extra.set("etapeMission", call.step);
    const bool first = c.messages.empty();
    if (first || c.title == kNewTitle) c.title = call.purpose == "plan" ? titleFor(text, false) : "Mission : " + m.name;
    if (first) {
        c.project.name = host_.projectName ? host_.projectName() : std::string{};
        c.project.path = host_.projectPath ? host_.projectPath() : std::string{};
    }
    if (const DemoContext ctx = context(); ctx.view) c.project.view = ctx.view->name;
    c.messages.push_back(std::move(u));
    // L'APPEL EST ECRIT AVANT DE PARTIR : une fermeture brutale le retrouvera (interrompu ; rien n'est rejoue).
    if (call.purpose == "proposition")
        for (const auto& s : call.steps) (void)mission::setStepState(m, s, mission::StepState::Running, when, "La proposition se pr\xC3\xA9pare.");
    m.pending.active = true;
    m.pending.purpose = call.purpose;
    m.pending.conversation = c.id;
    m.pending.started = when;
    m.pending.steps = call.purpose == "correction" ? std::vector<std::string>{call.step} : call.steps;
    if (!listed(m.conversations, c.id)) m.conversations.push_back(c.id);
    m.provider = settings_.provider;
    m.model = trim(settings_.model);
    if (const auto doc = host_.document ? host_.document() : nullptr) {
        // le projet tel que la mission le connait maintenant (ce qui a change avant est dans la consigne de l'appel)
        mission::stamp(m, mission::printOf(doc->project), when);
        m.coherence = mission::Coherence::Same;
        m.coherenceText.clear();
    }
    if (call.purpose == "plan")
        mission::log(m, "analyse", "Analyse du projet r\xC3\xA9" "el et plan demand\xC3\xA9s \xC3\xA0 l'IA (" + providerName(settings_.provider) + ").", when);
    else if (call.purpose == "actualisation")
        mission::log(m, "actualisation",
                     "Actualisation demand\xC3\xA9" "e \xC3\xA0 l'IA : relire le projet et revoir le plan"
                         + (call.changes.empty() ? std::string(".") : " (chang\xC3\xA9 : " + call.changes + ")."),
                     when);
    else if (call.purpose == "correction")
        mission::log(m, "proposition", "Correction demand\xC3\xA9" "e \xC3\xA0 l'IA pour " + call.step + ".", when, call.step);
    else
        mission::log(m, "proposition",
                     "Proposition demand\xC3\xA9" "e \xC3\xA0 l'IA pour " + joined(call.steps, ", ") + " (" + std::to_string(call.batch.ops)
                         + " op\xC3\xA9ration(s) estim\xC3\xA9" "e(s)).",
                     when);
    saveMission(m);
    nextMission_ = std::move(call);
    if (!startLive(c, text, &w)) {
        nextMission_ = {};
        c = before;
        if (mission::Mission* again = missionMut(id)) {
            *again = saved;
            mission::log(*again, "erreur", "La demande \xC3\xA0 l'IA n'est pas partie : " + w, when);
            saveMission(*again);
        }
        return fail(w);
    }
    pending_.clear();
    suggestionsAfter_ = false;
    if (Conversation* cc = touch(c)) save(*cc);
    ++revision_;
    return true;
}

// Les consignes d'un appel de mission (ajoutees aux consignes dynamiques de la demande).
std::string Assistant::missionPrompt(const mission::Mission& m, const MissionCall& call, const hmi::Project* project) const {
    if (call.purpose == "proposition" && project) return mission::batchPrompt(m, *project, call.batch, call.changes);
    if (call.purpose == "correction" && project) return mission::correctionPrompt(m, *project, call.step);
    std::string s = mission::planningPrompt(m);
    if (call.purpose == "actualisation")
        s += "ACTUALISATION : relis le projet tel qu'il est MAINTENANT (il fait foi, pas le plan) et renvoie le plan COMPLET avec "
             "planifier_mission. Ce qui a chang\xC3\xA9 depuis la derni\xC3\xA8re sauvegarde de la mission : "
             + (call.changes.empty() ? std::string("rien de connu") : call.changes) + ".\n";
    return s;
}

// LE PLAN RECU (planifier_mission) : planFromTool le lit et le controle (une etape sans titre, une dependance inconnue, un
// cycle : refuse, la mission n'est pas touchee) ; un plan REVU garde les etapes deja travaillees (la regle de
// mission::planFromTool : leur etat, leurs propositions ; une etape terminee absente du plan revu est gardee).
bool Assistant::acceptPlan(const std::string& id, const std::string& input, std::string* why, int* count) {
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (m->state == mission::State::Cancelled) {
        if (why) *why = "la mission est annul\xC3\xA9" "e : son plan n'est plus modifi\xC3\xA9";
        return false;
    }
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    if (!mission::planFromTool(*m, input, when, why)) return false;
    m->pending = {};
    if (const auto doc = host_.document ? host_.document() : nullptr) {
        mission::stamp(*m, mission::printOf(doc->project), when);
        m->coherence = mission::Coherence::Same;
        m->coherenceText.clear();
    }
    int n = 0;
    for (const auto& s : m->steps) n += s.state != mission::StepState::Cancelled;
    if (count) *count = n;
    missionTransition(*m, before);
    saveMission(*m);
    return true;
}

// LA REPONSE D'UN APPEL DE MISSION (finishLive, le message fini).
void Assistant::missionAnswered(const MissionCall& call, Conversation& c, int message, bool planned, const std::string& stopReason) {
    mission::Mission* m = missionMut(call.id);
    if (!m || missionsReadOnly_.count(call.id) || message < 0 || message >= static_cast<int>(c.messages.size())) return;
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    {
        Message& msg = c.messages[static_cast<std::size_t>(message)];
        msg.extra.set("mission", call.id);
    }
    // ---- le plan (planifier, actualiser) ----
    if (call.purpose == "plan" || call.purpose == "actualisation") {
        m->pending = {};
        const Message& msg = c.messages[static_cast<std::size_t>(message)];
        if (planned) {
            const bool revised = call.purpose == "actualisation" || m->planRevision > 0;
            int live = 0;
            for (const auto& s : m->steps) live += s.state != mission::StepState::Cancelled;
            // le plan en clair, ecrit par le LOGICIEL sous la reponse (jamais renvoye au modele comme sa reponse)
            Message a;
            a.id = "m" + std::to_string(c.messages.size() + 1);
            a.role = "assistant";
            a.date = when;
            a.state = "fini";
            a.text = planMessage(*m, revised);
            a.extra = json::Value::object();
            a.extra.set("logiciel", std::string("mission"));
            a.extra.set("mission", call.id);
            c.messages.push_back(std::move(a));
            notifyMission(*m, "plan", std::string(revised ? "Plan revu : " : "Plan termin\xC3\xA9 : ") + m->name,
                          plural(static_cast<std::size_t>(live), "\xC3\xA9tape", "\xC3\xA9tapes") + " ordonn\xC3\xA9" "es par leurs d\xC3\xA9pendances. Prochaine action : "
                              + mission::nextAction(*m),
                          "ok");
        } else {
            const std::string reason = !msg.note.empty() ? msg.note
                                       : stopReason == "max_tokens"
                                           ? std::string("la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e avant le plan")
                                           : std::string("l'IA n'a pas appel\xC3\xA9 planifier_mission : aucun plan n'est enregistr\xC3\xA9");
            mission::log(*m, "erreur", callLabel(call.purpose, call.step) + " n'a pas abouti : " + reason, when);
            notifyMission(*m, "erreur", "Erreur dans la mission : " + m->name, callLabel(call.purpose, call.step) + " n'a pas abouti : " + reason, "error");
        }
        missionTransition(*m, before);
        saveMission(*m);
        return;
    }
    // ---- une proposition (la suite, une correction) ----
    if (!c.messages[static_cast<std::size_t>(message)].proposal) {
        const Message& msg = c.messages[static_cast<std::size_t>(message)];
        const bool cut = stopReason == "max_tokens";
        const std::string reason = !msg.note.empty() ? msg.note : std::string("l'IA n'a rien propos\xC3\xA9 (aucune op\xC3\xA9ration)");
        endMissionCall(call, cut, reason);
        if (cut)
            if (const mission::Mission* again = findMission(call.id))
                notifyMission(*again, "interrompue", "Mission interrompue : " + again->name, callLabel(call.purpose, call.step) + " a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e : " + reason, "warning");
        return;
    }
    Message& msg = c.messages[static_cast<std::size_t>(message)];
    Proposal& p = *msg.proposal;
    const std::vector<std::string> steps = call.purpose == "correction" ? std::vector<std::string>{call.step} : call.steps;
    // le lien garde avec la proposition (et dans sa conversation) : la mission, ses etapes
    p.extra.set("mission", call.id);
    {
        json::Value list = json::Value::array();
        for (const auto& s : steps) list.push(s);
        p.extra.set("etapes", list);
    }
    mission::onGenerated(*m, p, c.id, msg.id, steps, when);
    m->pending = {};
    bool stale = false;
    if (const auto doc = host_.document ? host_.document() : nullptr) {
        // VERIFIEE SUR UNE COPIE (validate) : validee, ou ses erreurs exactes ; preparee sur un projet qui a change
        // pendant la reponse : obsolete (elle ne s'appliquera jamais, rien n'est ecrase).
        const Check ck = validate(doc->project, p, host_.env);
        mission::onChecked(*m, p.id, ck, when);
        if (ck.stale) {
            mission::onDropped(*m, p.id, "obsolete",
                               "le projet a chang\xC3\xA9 pendant la pr\xC3\xA9paration de la proposition : elle ne s'applique pas sur un \xC3\xA9tat qui n'est "
                               "plus le sien",
                               when);
            stale = true;
        }
    }
    const mission::ProposalLink* link = m->proposal(p.id);
    const std::string P = link ? "P" + std::to_string(link->order) : std::string("La proposition");
    if (stale)
        notifyMission(*m, "proposition", "Proposition obsol\xC3\xA8te : " + m->name,
                      P + " a \xC3\xA9t\xC3\xA9 pr\xC3\xA9par\xC3\xA9" "e sur un projet qui a chang\xC3\xA9 depuis : elle ne s'applique pas. Demande la suite pour en avoir une \xC3\xA0 jour.",
                      "warning");
    else
        notifyMission(*m, "proposition", "Proposition disponible : " + m->name,
                      P + " " + guillemets(p.summary) + " pour " + stepsLine(*m, link ? link->steps : steps) + " : \xC3\xA0 pr\xC3\xA9visualiser et appliquer.", "info");
    missionTransition(*m, before);
    saveMission(*m);
}

// UN APPEL DE MISSION FINI SANS RESULTAT (arrete, coupe, en erreur, sans proposition) : ses etapes << en cours >>
// redeviennent pretes, l'appel est efface, l'erreur exacte est dans l'historique. Rien n'est rejoue.
void Assistant::endMissionCall(const MissionCall& call, bool interrupted, const std::string& reason) {
    if (call.id.empty()) return;
    mission::Mission* m = missionMut(call.id);
    if (!m || missionsReadOnly_.count(call.id)) return;
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    for (const auto& s : call.steps)
        if (const mission::Step* st = m->step(s); st && st->state == mission::StepState::Running)
            (void)mission::setStepState(*m, s, mission::StepState::Ready, when,
                                        std::string(interrupted ? "Appel interrompu" : "Appel en \xC3\xA9" "chec") + " : " + reason);
    m->pending = {};
    mission::log(*m, interrupted ? "interruption" : "erreur",
                 callLabel(call.purpose, call.step) + (interrupted ? " a \xC3\xA9t\xC3\xA9 interrompue" : " a \xC3\xA9" "chou\xC3\xA9") + " : " + reason
                     + ". Rien n'est rejou\xC3\xA9.",
                 when, call.purpose == "correction" ? call.step : std::string{});
    mission::refreshReadiness(*m);
    mission::refreshState(*m, when);
    if (!interrupted)
        notifyMission(*m, "erreur", "Erreur dans la mission : " + m->name, callLabel(call.purpose, call.step) + " a \xC3\xA9" "chou\xC3\xA9 : " + reason, "error");
    missionTransition(*m, before);
    saveMission(*m);
}

// UNE PROPOSITION DE MISSION APPLIQUEE (afterApply) : son compte rendu REEL passe a la mission. Vrai : c'etait une
// proposition de mission (sa notification est celle de la mission).
bool Assistant::missionApplied(const Proposal& prop, const plan::Plan& pl, const plan::Report* rep) {
    mission::Mission* m = missionOfProposalMut(prop.id);
    if (!m) return false;
    if (missionsReadOnly_.count(m->id)) return true;
    const auto doc = host_.document ? host_.document() : nullptr;
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    std::map<std::string, mission::StepState> was;
    for (const auto& s : m->steps) was[s.id] = s.state;
    if (doc && rep) {
        mission::onApplied(*m, prop.id, pl, *rep, doc->project, when);
        mission::stamp(*m, mission::printOf(doc->project), when);
        m->coherence = mission::Coherence::Same;
        m->coherenceText.clear();
    } else {
        mission::log(*m, "application",
                     "Proposition appliqu\xC3\xA9" "e, mais son compte rendu r\xC3\xA9" "el n'a pas pu \xC3\xAAtre lu (aucun projet ouvert) : ses \xC3\xA9tapes restent \xC3\xA0 v\xC3\xA9rifier.",
                     when, {}, prop.id);
    }
    const mission::ProposalLink* link = m->proposal(prop.id);
    const std::string P = link ? "P" + std::to_string(link->order) : std::string("La proposition");
    notifyMission(*m, "appliquee", "Proposition appliqu\xC3\xA9" "e : " + m->name,
                  P + " " + guillemets(prop.summary) + (rep && !rep->verdict.empty() ? " \xE2\x80\x94 " + rep->verdict : std::string{}) + " Ctrl+Z la reprend d'un coup.",
                  rep && rep->failed > 0 ? "warning" : "ok");
    for (const auto& s : m->steps) {
        const auto it = was.find(s.id);
        const mission::StepState prev = it == was.end() ? mission::StepState::Waiting : it->second;
        if (s.state == mission::StepState::Done && prev != mission::StepState::Done)
            notifyMission(*m, "etape", "\xC3\x89tape termin\xC3\xA9" "e : " + m->name, s.id + " " + s.title + " \xE2\x80\x94 v\xC3\xA9rifi\xC3\xA9" "e dans le projet.", "ok");
        if (s.state == mission::StepState::Error && prev != mission::StepState::Error)
            notifyMission(*m, "erreur", "Erreur dans la mission : " + m->name, s.id + " " + s.title + " : " + (s.error.empty() ? s.note : s.error), "error");
    }
    missionTransition(*m, before);
    saveMission(*m);
    return true;
}

// Une proposition de mission refusee, ou devenue obsolete : ses etapes redeviennent pretes.
void Assistant::missionDropped(std::string_view proposalId, std::string_view state, const std::string& reason) {
    mission::Mission* m = missionOfProposalMut(proposalId);
    if (!m || missionsReadOnly_.count(m->id)) return;
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    mission::onDropped(*m, proposalId, state, reason, when);
    if (state == "obsolete")
        notifyMission(*m, "proposition", "Proposition obsol\xC3\xA8te : " + m->name,
                      reason + ". Demande la suite : elle partira du projet tel qu'il est.", "warning");
    missionTransition(*m, before);
    saveMission(*m);
}

bool Assistant::missionMayApply(std::string_view proposalId, std::string* why) const {
    const mission::Mission* m = missionOfProposal(proposalId);
    if (!m) return true;
    if (!mission::mayApply(*m, proposalId, why)) return false;
    if (!m->project.id.empty() && openProjectId() != m->project.id) {
        if (why)
            *why = "Cette proposition appartient \xC3\xA0 la mission " + guillemets(m->name) + ", du projet "
                   + guillemets(m->project.name.empty() ? std::string("?") : m->project.name) + " : ouvre ce projet pour l'appliquer.";
        return false;
    }
    return true;
}

// AU TICK : les missions interrompues (constatees des que le projet ouvert est connu), la verification des missions
// quand le projet a change (sa revision, ou un autre document), les audits finis.
void Assistant::missionsTick() {
    pollAudits();
    const auto doc = host_.document ? host_.document() : nullptr;
    // LA REPRISE APRES UNE FERMETURE : un appel a l'IA etait en cours - marque interrompu, ses etapes pretes, le projet
    // compare (identique, modifie, introuvable) ; jamais une application rejouee.
    if (!missionsToRecover_.empty() && (doc || !host_.document)) {
        const std::string pid = openProjectId();
        const std::string when = isoDate(now());
        const std::vector<std::string> ids = std::exchange(missionsToRecover_, {});
        for (const auto& id : ids) {
            mission::Mission* m = missionMut(id);
            if (!m || missionsReadOnly_.count(id) || !m->pending.active) continue;
            const std::string purpose = m->pending.purpose;
            const mission::State before = m->state;
            mission::Recovery rec;
            (void)mission::recover(*m, pid, doc ? &doc->project : nullptr, when, &rec);
            m->pending = {};
            notifyMission(*m, "interrompue", "Mission interrompue : " + m->name,
                          "Un appel \xC3\xA0 l'IA (" + (purpose.empty() ? std::string("proposition") : purpose) + ") \xC3\xA9tait en cours \xC3\xA0 la fermeture : il est "
                              "marqu\xC3\xA9 interrompu, rien n'est rejou\xC3\xA9. " + rec.text,
                          "warning");
            missionTransition(*m, before);
            saveMission(*m);
        }
    }
    if (!doc) {
        missionDoc_ = nullptr;
        missionSeenDoc_ = nullptr;
        return;
    }
    // LE PROJET A CHANGE (sa revision, ou un autre document) : verifie une fois qu'il s'est pose (un geste continu - un
    // deplacement a la souris - ne relance pas la verification a chaque image) ; le premier projet vu : tout de suite.
    const std::uint64_t rev = doc->revision;
    if (doc.get() == missionDoc_ && rev == missionDocRev_) return;   // deja verifie : rien a recalculer
    if (doc.get() != missionSeenDoc_ || rev != missionSeenRev_) {
        const bool first = missionDoc_ == nullptr;
        missionSeenDoc_ = doc.get();
        missionSeenRev_ = rev;
        missionSeenAt_ = lastNow_;
        if (!first) return;
    } else if (lastNow_ - missionSeenAt_ < kMissionSettle) {
        return;
    }
    missionDoc_ = doc.get();
    missionDocRev_ = rev;
    const std::string pid = openProjectId();
    if (pid.empty()) return;
    const std::string when = isoDate(now());
    for (auto& m : missions_) {
        if (m.archived || m.state == mission::State::Cancelled || m.proposals.empty() || missionsReadOnly_.count(m.id)) continue;
        if (m.project.id != pid) continue;   // une mission d'un autre projet ne se juge pas sur celui-ci
        const mission::State before = m.state;
        // LE PROJET DU MOMENT FAIT FOI : Ctrl+Z (la proposition annulee, l'etape redevient prete), Ctrl+Y (retablie).
        if (!mission::verifyAgainst(m, doc->project, when)) continue;
        missionTransition(m, before);
        saveMission(m);
    }
}

void Assistant::pollAudits() {
    for (auto it = audits_.begin(); it != audits_.end();) {
        const std::shared_ptr<AuditJob> job = *it;
        if (!job || !job->finished.load(std::memory_order_acquire)) {
            ++it;
            continue;
        }
        if (job->worker.joinable()) job->worker.join();
        it = audits_.erase(it);
        ++revision_;
        mission::Mission* m = missionMut(job->mission);
        if (!m || missionsReadOnly_.count(job->mission)) continue;
        const std::string when = isoDate(now());
        // le rapport : sur la copie auditee ; l'audit impossible est dit << non verifiable >>, jamais << verifie >>
        mission::FinalReport rep = mission::finalReport(*m, *job->copy, job->result.get(), when);
        if (job->plcUnknown)
            rep.toCheck.push_back("Les noms de l'automate n'ont pas pu \xC3\xAAtre relus pour l'audit : les r\xC3\xA9" "f\xC3\xA9rences \xC3\xA0 l'automate sont \xC3\xA0 v\xC3\xA9rifier.");
        if (const auto doc = host_.document ? host_.document() : nullptr; !doc || doc.get() != job->doc || doc->revision != job->revision)
            rep.toCheck.push_back("Le projet a chang\xC3\xA9 pendant l'audit : relance-le pour v\xC3\xA9rifier l'\xC3\xA9tat actuel.");
        m->finalReport = mission::reportToJson(rep);
        const mission::State before = m->state;
        mission::log(*m, "audit", "Audit final : " + rep.verdict, when);
        mission::refreshState(*m, when);
        missionTransition(*m, before);   // terminee (seulement si le rapport le dit complet) ou bloquee : notifiee
        if (!(rep.complete && m->state == mission::State::Done))
            notifyMission(*m, "audit", "Audit final : " + m->name, rep.verdict, rep.auditRun && rep.auditErrors == 0 ? "info" : "warning");
        saveMission(*m);
        notice_ = "Assistant IA : mission " + guillemets(m->name) + " \xE2\x80\x94 " + rep.verdict;
    }
}

// Reessayer une demande de mission : redemandee comme une demande de mission (le lot recalcule sur le projet reel).
bool Assistant::relaunchMission(const Message& request, std::string* why) {
    const std::string id = request.extra.str("mission");
    const std::string purpose = request.extra.str("appelMission");
    if (purpose == "proposition") return continueMission(id, why);
    if (purpose == "correction") return correctStep(id, request.extra.str("etapeMission"), why);
    if (purpose == "actualisation") return refreshMission(id, why);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    settleStale(*m);
    if (m->state == mission::State::Cancelled || m->archived) {
        if (why) *why = callBlocker(*m);
        return false;
    }
    if (!m->steps.empty()) return refreshMission(id, why);
    MissionCall call;
    call.purpose = "plan";
    return launchMission(*m, m->request, std::move(call), why);
}

// ---- l'API publique ----------------------------------------------------------------------------------------------------
const mission::Mission* Assistant::activeMission() const noexcept {
    const Conversation* c = current();
    if (!c) return nullptr;
    const mission::Mission* best = nullptr;
    for (const auto& m : missions_) {
        if (m.archived || m.state == mission::State::Cancelled) continue;
        if (m.conversation != c->id && !listed(m.conversations, c->id)) continue;
        if (!best || m.modified > best->modified) best = &m;
    }
    return best;
}

bool Assistant::suggestsMission(std::string_view request) const {
    if (!settings_.enabled || !live() || !mission::looksComplex(request)) return false;
    const Conversation* c = current();
    if (!c) return true;
    for (const auto& m : missions_) {
        if (m.archived || (m.conversation != c->id && !listed(m.conversations, c->id))) continue;
        if (m.state == mission::State::Planning || m.state == mission::State::Active || m.state == mission::State::Paused
            || m.state == mission::State::Blocked)
            return false;   // une mission est deja en cours ici
    }
    return true;
}

bool Assistant::createMission(std::string request, std::string* why) {
    request = trim(std::move(request));
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (request.empty()) return fail("D\xC3\xA9" "cris ce que la mission doit construire.");
    if (!settings_.enabled) return fail("L'Assistant est d\xC3\xA9sactiv\xC3\xA9 (Param\xC3\xA8tres de l'Assistant IA).");
    if (!live())
        return fail("Une mission travaille avec une IA connect\xC3\xA9" "e (Anthropic, ou un service compatible OpenAI) : choisis-la dans les "
                    "Param\xC3\xA8tres de l'Assistant IA.");
    if (generating()) return fail("Une r\xC3\xA9ponse est en cours d'\xC3\xA9" "criture : attends-la, ou arr\xC3\xAAte-la.");
    std::string w;
    if (!ready(&w)) return fail(w);
    if (toolsInText())
        return fail("Ce mod\xC3\xA8le re\xC3\xA7oit les propositions en texte : il ne peut pas appeler l'outil du plan d'une mission. Choisis "
                    "\xC2\xAB Outil \xC2\xBB (l'appel d'outil) dans les Param\xC3\xA8tres de l'Assistant IA.");
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return fail("Aucun projet n'est ouvert : une mission travaille sur le projet IHM ouvert.");
    if (!cur()) newConversation();
    const Conversation* c = cur();
    if (!c) return fail("Aucune conversation.");
    const std::string when = isoDate(now());
    mission::Mission m = mission::make(request, c->id, when);
    m.project.name = host_.projectName ? host_.projectName() : std::string{};
    m.project.path = host_.projectPath ? host_.projectPath() : std::string{};
    m.project.id = openProjectId();
    m.provider = settings_.provider;
    m.model = trim(settings_.model);
    // les pieces jointes de la conversation, par leur identifiant (jamais copiees)
    for (const auto& f : attach::buildContext(*c).files)
        if (!f.attachment.removed && !f.attachment.id.empty()) m.attachments.push_back(f.attachment.id);
    for (const auto& p : pending_)
        if (!p.attachment.id.empty()) m.attachments.push_back(p.attachment.id);
    const std::string id = m.id;
    missions_.insert(missions_.begin(), std::move(m));
    MissionCall call;
    call.purpose = "plan";
    if (!launchMission(missions_.front(), request, std::move(call), why)) {
        // la demande n'est pas partie : la mission n'existe pas (ni ici, ni sur le disque)
        missions_.erase(std::remove_if(missions_.begin(), missions_.end(), [&id](const mission::Mission& x) { return x.id == id; }), missions_.end());
        (void)mission::remove(id);
        ++missionsRev_;
        ++revision_;
        return false;
    }
    if (const mission::Mission* made = findMission(id)) {
        notifyMission(*made, "creee", "Mission cr\xC3\xA9\xC3\xA9" "e : " + made->name,
                      "L'IA analyse le projet r\xC3\xA9" "el et pr\xC3\xA9pare le plan ; rien n'est modifi\xC3\xA9 avant ta validation.", "info");
        notice_ = "Assistant IA : mission " + guillemets(made->name) + " cr\xC3\xA9\xC3\xA9" "e - l'IA analyse le projet et pr\xC3\xA9pare le plan.";
    }
    appendActivity("mission", "Mission cr\xC3\xA9\xC3\xA9" "e : " + request);
    return true;
}

bool Assistant::continueMission(std::string_view idv, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    settleStale(*m);
    if (const std::string b = callBlocker(*m); !b.empty()) return fail(b);
    if (m->state == mission::State::Planning)
        return fail(m->pending.active ? std::string("L'IA pr\xC3\xA9pare le plan de la mission : attends sa r\xC3\xA9ponse.")
                                      : std::string("Le plan de la mission n'est pas encore \xC3\xA9tabli : actualise la mission pour le demander."));
    const auto doc = host_.document();
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    // LE PROJET REEL D'ABORD : ce que les propositions precedentes ont cree y est-il encore ? Les etapes pretes, ensuite.
    const bool verified = mission::verifyAgainst(*m, doc->project, when);
    mission::refreshReadiness(*m);
    mission::refreshState(*m, when);
    const mission::Batch batch = mission::nextBatch(*m, missionMaxOps(outputTokens()));
    if (batch.steps.empty()) {
        if (verified || m->state != before) {
            missionTransition(*m, before);
            saveMission(*m);
        }
        return fail(batch.why.empty() ? std::string("Rien n'est r\xC3\xA9" "alisable pour l'instant.") : batch.why);
    }
    // ce qui a change depuis la derniere empreinte de la mission (une modification a la main, un Ctrl+Z...)
    std::vector<std::string> changes;
    if (!m->project.files.empty()) changes = mission::changesBetween(m->project.files, mission::printOf(doc->project).files);
    MissionCall call;
    call.purpose = "proposition";
    call.steps = batch.steps;
    call.batch = batch;
    call.changes = joined(changes, ", ");
    const std::string text = "Propose la suite de la mission " + guillemets(m->name) + " : " + stepsLine(*m, batch.steps) + ".";
    return launchMission(*m, text, std::move(call), why);
}

bool Assistant::correctStep(std::string_view idv, std::string_view step, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    settleStale(*m);
    if (const std::string b = callBlocker(*m); !b.empty()) return fail(b);
    const mission::Step* st = m->step(step);
    if (!st) return fail("\xC3\x89tape inconnue : " + std::string(step) + ".");
    if (st->state != mission::StepState::Error)
        return fail("L'\xC3\xA9tape " + st->id + " " + guillemets(st->title) + " n'est pas en erreur (" + std::string(mission::stepLabel(st->state))
                    + ") : rien \xC3\xA0 corriger.");
    MissionCall call;
    call.purpose = "correction";
    call.step = st->id;
    call.steps = {st->id};
    const std::string text = "Propose une correction pour l'\xC3\xA9tape " + st->id + " " + guillemets(st->title) + " de la mission " + guillemets(m->name)
                             + " : " + (st->error.empty() ? st->note : st->error);
    return launchMission(*m, text, std::move(call), why);
}

bool Assistant::refreshMission(std::string_view idv, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    settleStale(*m);
    if (const std::string b = callBlocker(*m); !b.empty()) return fail(b);
    if (toolsInText())
        return fail("Ce mod\xC3\xA8le re\xC3\xA7oit les propositions en texte : il ne peut pas appeler l'outil du plan d'une mission. Choisis "
                    "\xC2\xAB Outil \xC2\xBB (l'appel d'outil) dans les Param\xC3\xA8tres de l'Assistant IA.");
    const auto doc = host_.document();
    std::vector<std::string> changes;
    if (!m->project.files.empty()) changes = mission::changesBetween(m->project.files, mission::printOf(doc->project).files);
    MissionCall call;
    call.purpose = m->steps.empty() ? "plan" : "actualisation";
    call.changes = joined(changes, ", ");
    const std::string text = m->steps.empty() ? m->request
                                              : "Actualise le plan de la mission " + guillemets(m->name)
                                                    + " : relis le projet tel qu'il est maintenant et revois les \xC3\xA9tapes restantes.";
    return launchMission(*m, text, std::move(call), why);
}

bool Assistant::auditMission(std::string_view idv, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (auditing(id)) return fail("L'audit final de cette mission est d\xC3\xA9j\xC3\xA0 en cours.");
    if (m->state == mission::State::Cancelled) return fail("La mission " + guillemets(m->name) + " est annul\xC3\xA9" "e.");
    if (m->steps.empty()) return fail("Le plan de la mission n'est pas encore \xC3\xA9tabli : rien \xC3\xA0 auditer.");
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return fail("Aucun projet n'est ouvert : l'audit se fait sur le projet de la mission.");
    if (!m->project.id.empty() && openProjectId() != m->project.id)
        return fail("Le projet ouvert n'est pas celui de la mission " + guillemets(m->name) + " : ouvre-le pour l'auditer.");
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    (void)mission::verifyAgainst(*m, doc->project, when);
    mission::refreshReadiness(*m);
    mission::refreshState(*m, when);
    // L'AUDIT SUR UNE COPIE, sur un fil (comme l'application de fond) : Generer, Compiler, les adresses, les references,
    // les dependances, les orphelins - le projet vivant n'est jamais lu depuis ce fil.
    auto job = std::make_shared<AuditJob>();
    job->mission = id;
    job->copy = std::make_shared<const hmi::Project>(doc->project);
    Env frozen;
    if (frozenEnvironment(frozen)) job->plc = frozen.plcName;   // nul : aucun automate relie
    else job->plcUnknown = true;
    job->folder = host_.projectPath ? host_.projectPath() : std::string{};
    job->doc = doc.get();
    job->revision = doc->revision;
    job->worker = std::thread([job] {
        try {
            job->result = std::make_shared<hmi::audit::Report>(hmi::audit::run(*job->copy, job->plc, job->folder));
        } catch (...) {
            job->result.reset();   // l'audit n'a pas pu se faire : le rapport le dira (non verifiable)
        }
        job->finished.store(true, std::memory_order_release);
    });
    audits_.push_back(std::move(job));
    mission::log(*m, "audit", "Audit final lanc\xC3\xA9 sur une copie du projet (G\xC3\xA9n\xC3\xA9rer, Compiler, adresses, r\xC3\xA9" "f\xC3\xA9rences, d\xC3\xA9pendances).", when);
    missionTransition(*m, before);
    saveMission(*m);
    notice_ = "Assistant IA : audit final de la mission " + guillemets(m->name) + " en arri\xC3\xA8re-plan (sur une copie du projet).";
    return true;
}

bool Assistant::auditing(std::string_view id) const {
    return std::any_of(audits_.begin(), audits_.end(), [&id](const std::shared_ptr<AuditJob>& j) { return j && j->mission == id; });
}

bool Assistant::pauseMission(std::string_view idv, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    // LA PAUSE INTERROMPT VRAIMENT : la reponse en cours pour cette mission est arretee (l'appel marque interrompu, ses
    // etapes pretes) ; plus aucune proposition de la mission ne s'applique (mayApply) tant qu'elle n'est pas reprise.
    if (generating() && gen_.mission.id == id) stop();
    m = missionMut(id);
    if (!m) return false;
    settleStale(*m);   // un appel reste << en cours >> d'une autre seance : interrompu
    const std::string when = isoDate(now());
    if (!mission::pause(*m, when, why)) return false;
    saveMission(*m);
    notice_ = "Assistant IA : mission " + guillemets(m->name) + " en pause - aucune proposition ne se pr\xC3\xA9pare ni ne s'applique.";
    return true;
}

bool Assistant::resumeMission(std::string_view idv, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (m->state == mission::State::Cancelled) return fail("La mission " + guillemets(m->name) + " est annul\xC3\xA9" "e : elle ne reprend pas.");
    const auto doc = host_.document ? host_.document() : nullptr;
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    // LE CONSTAT D'ABORD (le projet ouvert compare a la mission) : rien n'est applique ni rejoue.
    mission::Recovery rec;
    (void)mission::recover(*m, openProjectId(), doc ? &doc->project : nullptr, when, &rec);
    if (!(generating() && gen_.mission.id == id)) m->pending = {};
    missionsToRecover_.erase(std::remove(missionsToRecover_.begin(), missionsToRecover_.end(), id), missionsToRecover_.end());
    if (rec.coherence == mission::Coherence::Missing) {
        saveMission(*m);
        return fail("La mission " + guillemets(m->name) + " ne reprend pas : " + rec.text + " " + rec.nextAction);
    }
    if (m->state == mission::State::Paused && !mission::resume(*m, when, why)) {
        saveMission(*m);
        return false;
    }
    mission::refreshReadiness(*m);
    mission::refreshState(*m, when);
    missionTransition(*m, before);
    saveMission(*m);
    notice_ = rec.coherence == mission::Coherence::Changed
                  ? "Assistant IA : mission " + guillemets(m->name) + " reprise - " + rec.text + " Actualise-la pour que l'IA en tienne compte."
                  : "Assistant IA : mission " + guillemets(m->name) + " reprise. Prochaine action : " + mission::nextAction(*m);
    return true;
}

bool Assistant::cancelMission(std::string_view idv, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (generating() && gen_.mission.id == id) stop();
    m = missionMut(id);
    if (!m) return false;
    const std::string when = isoDate(now());
    // L'ANNULATION EMPECHE TOUTE APPLICATION ULTERIEURE (mayApply) ; ce qui est deja applique reste dans le projet.
    if (!mission::cancel(*m, when, why)) return false;
    saveMission(*m);
    notice_ = "Assistant IA : mission " + guillemets(m->name) + " annul\xC3\xA9" "e - aucune de ses propositions ne s'appliquera plus.";
    return true;
}

bool Assistant::archiveMission(std::string_view idv, bool on, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (on && generating() && gen_.mission.id == id) {
        if (why) *why = "Une r\xC3\xA9ponse de l'IA est en cours pour cette mission : attends-la, ou arr\xC3\xAAte-la.";
        return false;
    }
    mission::archive(*m, on, isoDate(now()));
    saveMission(*m);
    return true;
}

bool Assistant::renameMission(std::string_view idv, std::string name, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    name = trim(std::move(name));
    if (name.empty()) {
        if (why) *why = "Le nom de la mission est vide.";
        return false;
    }
    if (name == m->name) return true;
    mission::log(*m, "decision", "Renomm\xC3\xA9" "e : " + guillemets(m->name) + " devient " + guillemets(name) + ".", isoDate(now()));
    m->name = std::move(name);
    saveMission(*m);
    return true;
}

bool Assistant::editMissionPlan(std::string_view idv, const mission::PlanEdit& edit, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (generating() && gen_.mission.id == id) {
        if (why) *why = "Une r\xC3\xA9ponse de l'IA est en cours pour cette mission : attends-la, ou arr\xC3\xAAte-la, avant de modifier le plan.";
        return false;
    }
    const std::string when = isoDate(now());
    const mission::State before = m->state;
    if (!mission::editPlan(*m, edit, when, why)) return false;
    mission::refreshReadiness(*m);
    mission::refreshState(*m, when);
    missionTransition(*m, before);
    saveMission(*m);
    return true;
}

bool Assistant::attachMission(std::string_view idv, std::string* why) {
    const std::string id(idv);
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (!cur()) newConversation();
    Conversation* c = cur();
    if (!c) return false;
    if (generating() && gen_.conversation == c->id) {
        if (why) *why = "Une r\xC3\xA9ponse est en cours dans cette conversation : attends-la, ou arr\xC3\xAAte-la.";
        return false;
    }
    m = missionMut(id);   // newConversation ne touche pas aux missions ; par prudence
    if (!m) return false;
    if (c->id == m->conversation || listed(m->conversations, c->id)) return true;   // deja la sienne
    const std::string when = isoDate(now());
    settleStale(*m);
    m->conversations.push_back(c->id);
    mission::log(*m, "decision", "Reprise dans la conversation " + guillemets(c->title) + ".", when);
    saveMission(*m);
    // dans la conversation : ou en est la mission (les donnees enregistrees), ecrit par le LOGICIEL
    Message a;
    a.id = "m" + std::to_string(c->messages.size() + 1);
    a.role = "assistant";
    a.date = when;
    a.state = "fini";
    a.text = "**Mission " + guillemets(m->name) + "** reprise dans cette conversation.\n\n" + mission::summaryOf(*m);
    a.extra = json::Value::object();
    a.extra.set("logiciel", std::string("mission"));
    a.extra.set("mission", id);
    c->messages.push_back(std::move(a));
    if (Conversation* moved = touch(*c)) save(*moved);
    ++revision_;
    return true;
}

bool Assistant::openMissionConversation(std::string_view id) {
    const mission::Mission* m = findMission(id);
    if (!m) return false;
    for (const auto& c : conversations_)   // les plus recentes d'abord
        if (c.id == m->conversation || listed(m->conversations, c.id)) {
            const std::string cid = c.id;
            return select(cid);
        }
    return false;
}

mission::Recovery Assistant::inspectMission(std::string_view id) const {
    const mission::Mission* m = findMission(id);
    if (!m) return {};
    const auto doc = host_.document ? host_.document() : nullptr;
    return mission::inspect(*m, openProjectId(), doc ? &doc->project : nullptr);
}

int Assistant::messageOfProposal(std::string_view proposalId) const {
    const Conversation* c = current();
    if (!c || proposalId.empty()) return -1;
    for (std::size_t i = 0; i < c->messages.size(); ++i)
        if (c->messages[i].proposal && c->messages[i].proposal->id == proposalId) return static_cast<int>(i);
    return -1;
}

} // namespace app::ai
