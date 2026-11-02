// =============================================================================
//  app/ai/AiAssistant.cpp - voir AiAssistant.hpp
// =============================================================================
#include "AiAssistant.hpp"
#include "AiLearning.hpp"       // 1.17.0 : les mesures de l'IA integree
#include "AiLearningFile.hpp"   // 1.17.0 : ce que l'installateur a depose

#include "AiAutomate.hpp"
#include "AiDelete.hpp"         // 1.22.7 : la strategie d'une suppression (Corriger rapidement)

#include "AiHttp.hpp"
#include "AiKeys.hpp"          // 1.18.0 : le gestionnaire des cles API
#include "AiLocal.hpp"
#include "AiLoop.hpp"            // 1.22.4 : ne pas tourner en rond (les empreintes, les signes, la retenue)
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
#include "../../core/Perf.hpp"       // 1.22.2 : ce qui consomme (l'onglet Performances) - des portees de mesure seulement

#include <algorithm>
#include <filesystem>   // 1.22.3 : le nom du .XPG lu
#include <cstdlib>
#include <cstring>   // 1.22.6 : strlen
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

// ---- 1.22.5 : L'AVIS DU LOGICIEL, LE PROCESSUS DE DEVELOPPEMENT ----
// "14:02" : l'heure de l'horloge de l'Assistant (celle des essais aussi).
std::string clockTime(const std::string& iso) { return iso.size() >= 16 ? iso.substr(11, 5) : iso; }

// "2 erreurs, 1 avertissement" ; "aucun constat nouveau".
std::string findingsText(std::size_t errors, std::size_t warnings) {
    if (errors == 0 && warnings == 0) return "aucun constat nouveau";
    std::string s;
    if (errors > 0) s += plural(errors, "erreur", "erreurs");
    if (warnings > 0) s += (s.empty() ? std::string{} : std::string(", ")) + plural(warnings, "avertissement", "avertissements");
    return s;
}

// Les etapes du logiciel en cause dans les ERREURS d'un verdict : "Compiler", "G\xC3\xA9n\xC3\xA9rer", ou les deux.
std::string errorSteps(const build::Verdict& v) {
    bool gen = false, comp = false;
    for (const auto& f : v.findings)
        if (f.error) (f.step == "Compiler" ? comp : gen) = true;
    if (gen && comp) return "G\xC3\xA9n\xC3\xA9rer et Compiler";
    return gen ? std::string("G\xC3\xA9n\xC3\xA9rer") : std::string("Compiler");
}
// "Compiler : 2 erreurs" (l'activite d'une etape de mission) ; "2 erreurs de Compiler" (la bande du haut).
std::string fixLabel(const std::string& who, int errors) {
    return who + " : " + plural(static_cast<std::size_t>(std::max(0, errors)), "erreur", "erreurs");
}
std::string fixWhat(const std::string& who, int errors) {
    return plural(static_cast<std::size_t>(std::max(0, errors)), "erreur", "erreurs") + " de " + who;
}

// L'entree << logiciel >> du processus : "G\xC3\xA9n\xC3\xA9rer et Compiler (copie du projet) : 2 erreurs, 1 avertissement \xE2\x80\x94 1,4 s".
std::string verdictLine(const build::Verdict& v) {
    if (!v.ran)
        return "G\xC3\xA9n\xC3\xA9rer et Compiler n'ont pas pu v\xC3\xA9rifier la proposition" + (v.why.empty() ? std::string(".") : " : " + v.why);
    const bool clean = v.errors == 0 && v.warnings == 0;
    return "G\xC3\xA9n\xC3\xA9rer et Compiler (copie du projet) : " + findingsText(v.errors, v.warnings) + (clean ? std::string(" \xE2\x9C\x93") : std::string{})
           + " \xE2\x80\x94 " + build::durationText(v.ms);
}

// La phrase de l'IA dans sa reponse (la premiere, sans la mise en forme), pour le processus : << j'ai remplace ... >>.
std::string firstSentence(std::string_view text, std::size_t max = 200) {
    std::string s;
    for (const char ch : text) {
        if (ch == '*' || ch == '`' || ch == '#') continue;
        if (ch == '\n' || ch == '\r') {
            if (!s.empty()) break;
            continue;
        }
        s += ch;
        if ((ch == '.' || ch == '!' || ch == '?') && s.size() > 12) break;
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == ':')) s.pop_back();
    while (!s.empty() && s.front() == ' ') s.erase(s.begin());
    if (s.size() > max) {
        std::size_t cut = max;
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;   // pas au milieu d'un caractere
        s = s.substr(0, cut) + "\xE2\x80\xA6";
    }
    return s;
}

// Le contenu d'une proposition, tel que Generer et Compiler le voient (ses operations, les retirees, les choix de variables).
std::string contentKey(const Proposal& p) {
    std::string k = loop::opsPrint(p) + "|" + std::to_string(p.ops.size()) + "|" + p.variable.use + (p.variable.create ? "+" : "-");
    for (const auto x : p.excluded) k += "x" + std::to_string(x);
    for (const auto& ch : p.choices) k += "|" + ch.from + ">" + ch.use + (ch.create ? "+" + ch.type : std::string{});
    return k;
}

// La proposition de ce message de cette conversation (nul : aucune).
Proposal* proposalIn(Conversation& c, std::string_view id, int* index = nullptr) {
    for (std::size_t i = 0; i < c.messages.size(); ++i)
        if (c.messages[i].proposal && c.messages[i].proposal->id == id) {
            if (index) *index = static_cast<int>(i);
            return &*c.messages[i].proposal;
        }
    return nullptr;
}

// ---- 1.22.6 : L'HISTORIQUE RESUME (ce qui part au fournisseur) ----
// Les operations d'une proposition, comptees par action, et les noms qu'elle cree ou touche (bornes) :
// "40 operations (creer x40) : Voyant_L1_0, Afficheur_L1_1, ... (+28)".
std::string opsDigest(const Proposal& p, std::size_t maxBytes = 420) {
    std::vector<std::pair<std::string, int>> counts;
    std::vector<std::string> names;
    const auto name = [&names](const std::string& n) {
        if (!n.empty() && std::find(names.begin(), names.end(), n) == names.end()) names.push_back(n);
    };
    for (const auto& op : p.ops) {
        const std::string k(actionKey(op.action));
        auto it = std::find_if(counts.begin(), counts.end(), [&k](const auto& c) { return c.first == k; });
        if (it == counts.end()) counts.emplace_back(k, 1);
        else ++it->second;
        name(op.name);
        for (const auto& v : op.variables) name(v.name);
        for (const auto& o : op.objects) name(o);
        for (const auto& n : op.names) name(n);
        if (op.action == Operation::Action::Address || op.action == Operation::Action::Bind) name(op.variable);
    }
    std::string s = plural(p.ops.size(), "op\xC3\xA9ration", "op\xC3\xA9rations") + " (";
    for (std::size_t i = 0; i < counts.size(); ++i)
        s += (i ? ", " : "") + counts[i].first + (counts[i].second > 1 ? " x" + std::to_string(counts[i].second) : std::string{});
    s += ")";
    std::string list;
    std::size_t shown = 0;
    for (const auto& n : names) {
        if (!list.empty() && list.size() + n.size() + 2 > maxBytes) break;
        list += (list.empty() ? "" : ", ") + n;
        ++shown;
    }
    if (!list.empty()) s += " : " + list + (shown < names.size() ? ", \xE2\x80\xA6 (+" + std::to_string(names.size() - shown) + ")" : std::string{});
    return s;
}

// 1.22.6 : la conversation `index` sans les pieces jointes qu'une autre conversation porte aussi (une mission sortie d'une
// conversation emporte ses pieces : la meme copie sur le disque) - ce que sa suppression peut effacer.
Conversation unshared(const std::vector<Conversation>& all, std::size_t index) {
    Conversation c = all[index];
    for (auto& msg : c.messages)
        std::erase_if(msg.attachments, [&](const Attachment& a) {
            for (std::size_t k = 0; k < all.size(); ++k) {
                if (k == index) continue;
                for (const auto& om : all[k].messages)
                    for (const auto& oa : om.attachments)
                        if (!a.id.empty() && oa.id == a.id) return true;
            }
            return false;
        });
    return c;
}

// ---- 1.22.6 : DE LA CONVERSATION A LA MISSION IA ----

bool adviceDeclined(const Conversation& c) { return c.extra.isObject() && c.extra.str("missionConseil") == "ici"; }

bool missionRunningIn(const std::vector<mission::Mission>& all, const std::string& conversation) {
    for (const auto& m : all) {
        if (m.archived || (m.conversation != conversation && !listed(m.conversations, conversation))) continue;
        if (m.state == mission::State::Planning || m.state == mission::State::Active || m.state == mission::State::Paused
            || m.state == mission::State::Blocked)
            return true;
    }
    return false;
}

std::string lowered(std::string_view s) {
    std::string out(s);
    for (auto& ch : out) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    return out;
}

bool alnum(char ch) { return std::isalnum(static_cast<unsigned char>(ch)) != 0; }

// Les etapes que la demande enumere : S3b, S5, S6... (une lettre S, un ou deux chiffres, une lettre au plus), sans doublon.
std::vector<std::string> stepTokens(std::string_view t) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < t.size(); ++i) {
        if ((t[i] != 'S' && t[i] != 's') || (i > 0 && alnum(t[i - 1]))) continue;
        std::size_t j = i + 1;
        while (j < t.size() && j - i <= 2 && std::isdigit(static_cast<unsigned char>(t[j]))) ++j;
        if (j == i + 1) continue;
        if (j < t.size() && std::isalpha(static_cast<unsigned char>(t[j])) && (j + 1 >= t.size() || !alnum(t[j + 1]))) ++j;
        if (j < t.size() && alnum(t[j])) continue;
        std::string tok(t.substr(i, j - i));
        tok[0] = 'S';
        if (std::find(out.begin(), out.end(), tok) == out.end()) out.push_back(std::move(tok));
    }
    return out;
}

// Un texte borne a `max` octets (sans couper un caractere), entier sinon.
std::string clipped(std::string s, std::size_t max) {
    if (s.size() <= max) return s;
    std::size_t cut = max;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    return s.substr(0, cut) + "\xE2\x80\xA6";
}

// Le sort d'une proposition, tel qu'il est enregistre (`undone` : appliquee puis reprise par Ctrl+Z).
std::string fateText(const Proposal& p, bool undone) {
    if (p.state == "appliquee") return undone ? "appliqu\xC3\xA9" "e puis annul\xC3\xA9" "e (Ctrl+Z)" : "appliqu\xC3\xA9" "e au projet";
    if (p.state == "refusee") return "refus\xC3\xA9" "e par l'utilisateur (rien n'a chang\xC3\xA9)";
    if (p.state == "remplacee") return "remplac\xC3\xA9" "e par ta proposition corrig\xC3\xA9" "e (jamais appliqu\xC3\xA9" "e)";
    return "en attente";
}

} // namespace

// 1.22.6 : un appel d'outil d'avant, RESUME pour le modele (son entree n'est plus renvoyee) - vide : il part entier.
std::string Assistant::compactCall(const Message& m) const {
    if (!m.tool) return {};
    const std::string head = "[R\xC3\xA9sum\xC3\xA9 du logiciel, ce tour n'est plus renvoy\xC3\xA9 en entier : ";
    const std::string tail = " Le projet tel qu'il est maintenant est dans le contexte de la derni\xC3\xA8re demande.]";
    if (m.tool->name == mission::kPlanTool) {
        if (!m.tool->error.empty()) return head + "ton plan de mission (planifier_mission) \xC3\xA9tait invalide : " + firstSentence(m.tool->error) + "]";
        const std::int64_t n = m.extra.integer("planEtapes");
        return head + "ton plan de mission (planifier_mission) a \xC3\xA9t\xC3\xA9 enregistr\xC3\xA9 ("
               + plural(static_cast<std::size_t>(std::max<std::int64_t>(0, n)), "\xC3\xA9tape", "\xC3\xA9tapes") + ")." + tail;
    }
    if (m.tool->name != kOperationsTool) return {};
    if (!m.tool->error.empty() || !m.proposal)
        return head + "ton appel de proposer_operations \xC3\xA9tait invalide, rien n'a \xC3\xA9t\xC3\xA9 propos\xC3\xA9"
               + (m.tool->error.empty() ? std::string(".") : " : " + firstSentence(m.tool->error)) + "]";
    const Proposal& p = *m.proposal;
    return head + "ta proposition \xC2\xAB " + p.summary + " \xC2\xBB (proposer_operations, " + opsDigest(p) + ") \xE2\x80\x94 "
           + fateText(p, p.state == "appliquee" && appliedOf(p) == Applied::Undone) + "." + tail;
}

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
    // 1.22.1 : les verifications de fond en cours (sur leur copie du projet)
    for (auto& j : checkJobs_)
        if (j && j->worker.joinable()) j->worker.join();
    // 1.22.5 : Generer et Compiler en cours (sur leur copie du projet)
    for (auto& j : buildJobs_)
        if (j && j->worker.joinable()) j->worker.join();
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
        (void)deleteConversationFiles(unshared(conversations_, static_cast<std::size_t>(i)));   // 1.22.6 : les pieces partagees restent
        appendActivity("nettoyage", "Conversation retir\xC3\xA9" "e (conservation de l'historique) : " + conversations_[static_cast<std::size_t>(i)].title);
        conversations_.erase(conversations_.begin() + i);
    }
    std::stable_sort(conversations_.begin(), conversations_.end(),
                     [](const Conversation& a, const Conversation& b) { return a.modified > b.modified; });
    current_ = conversations_.empty() ? -1 : 0;
    retire();
    gen_ = {};
    pending_.clear();
    fixQueue_.clear();        // 1.22.5 : les corrections automatiques en attente ne survivent pas a une relecture
    directWaiting_.clear();
    nextFix_ = {};
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
    XPG_MESURE("ia.tick");   // 1.22.2 : l'onglet Performances (core/Perf.hpp)
    core::perf::Lap lap;
    lastNow_ = now;
    const std::uint64_t before = revision_;
    // 1.21.0 : les analyses des pieces jointes finies (le fil d'analyse), remises dans leur conversation ; les souvenirs
    // que l'IA a proposes pendant sa reponse - a valider par l'utilisateur, dit une fois.
    lap.to(XPG_PARTIE("ia.tick.analyses"));
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
    lap.to(XPG_PARTIE("ia.tick.pont"));
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
    lap.stop();
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
    lap.to(XPG_PARTIE("ia.tick.application"));
    pollApply();   // 1.20.1 : l'application de fond finie : sa commande, sur ce fil
    lap.to(XPG_PARTIE("ia.tick.verifications"));
    pollChecks();  // 1.22.1 : les verifications de fond finies : les comptes, la mission
    pollBuilds();  // 1.22.5 : l'avis du logiciel (Generer et Compiler) arrive : le verdict, la correction a demander
    runFixes();    //          une correction automatique decidee part (aucune reponse en cours)
    lap.to(XPG_PARTIE("ia.tick.missions"));
    missionsTick();   // 1.22.0 : la reprise au demarrage, le projet change (Ctrl+Z...), les audits finis
    lap.to(XPG_PARTIE("ia.tick.direct"));
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
    (void)deleteConversationFiles(unshared(conversations_, static_cast<std::size_t>(i)));   // 1.22.6 : les pieces partagees restent
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
    // 1.22.4 : apres << Modifier la demande >> sur une proposition de mission qui tournait en rond, le texte qui commence par
    // << Pour S4 : >> est une CONSIGNE de la mission (gardee) et la suite repart avec elle ; un autre envoi l'oublie.
    if (steer_) {
        const Conversation* c0 = cur();
        if (c0 && c0->id == steer_->conversation && text.rfind(trim(steer_->prefix), 0) == 0) return steerMission(text, why);
        steer_.reset();
    }
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
        std::string plcRead = readPlcAttachments(u.attachments);   // avant le contexte de la demande
        c.messages.push_back(std::move(u));
        if (plcRead.empty()) plcRead = ensurePlc(c);   // 1.22.3 : un .XPG joint plus tot (une autre seance)
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

std::string Assistant::ensurePlc(const Conversation& c, const mission::Mission* m) {
    if (!host_.readPlc) return {};
    const Attachment* latest = nullptr;
    for (const auto& msg : c.messages)
        for (const auto& a : msg.attachments)
            if (!a.removed && isPlcExport(a.name)) latest = &a;
    if (!latest && m)   // les pieces de la mission (leur identifiant), dans ses conversations
        for (const auto& id : m->attachments)
            for (const auto& conv : conversations_)
                for (const auto& msg : conv.messages)
                    for (const auto& a : msg.attachments)
                        if (a.id == id && !a.removed && isPlcExport(a.name)) latest = &a;
    if (!latest) return {};
    // le nom du fichier lu (readPlcFile garde le nom de la copie : pieces-jointes/<id>/<nom>)
    const std::string path = attachmentPath(*latest);
    const auto u8 = std::filesystem::path(std::u8string(path.begin(), path.end())).filename().u8string();
    const std::string fileName = u8.empty() ? latest->name : std::string(u8.begin(), u8.end());
    const std::string loaded = host_.env.plcSource ? host_.env.plcSource() : std::string{};
    if (!loaded.empty() && loaded == fileName) return {};   // deja l'automate de la seance
    return readPlcAttachments({*latest});
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
    bool countLater = false;   // 1.22.1 : une proposition lourde, verifiee sur un fil de fond
    if (gen_.proposal) {
        Proposal p = std::move(*gen_.proposal);
        p.state = "proposee";
        if (const auto doc = host_.document ? host_.document() : nullptr) {
            if (checkInBackground(p)) {
                countLater = true;
            } else {
                const Check ck = validate(doc->project, p, host_.env);
                p.create = ck.create;
                p.modify = ck.modify;
                p.remove = ck.remove;
            }
        }
        m.proposal = std::move(p);
        // 1.22.5 : l'avis du logiciel accompagne les propositions de l'IA connectee - dit (la previsualisation ne l'attend pas)
        noVerdict(*m.proposal, "l'avis du logiciel (G\xC3\xA9n\xC3\xA9rer et Compiler) accompagne les propositions de l'IA connect\xC3\xA9" "e");
        if (countLater && !startCheck(c->id, *m.proposal, CheckFor::Counts))
            if (const auto doc = host_.document ? host_.document() : nullptr) {
                const Check ck = validate(doc->project, *m.proposal, host_.env);
                m.proposal->create = ck.create;
                m.proposal->modify = ck.modify;
                m.proposal->remove = ck.remove;
            }
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
    const std::string fixOf = !call.fixOf.empty() ? call.fixOf : gen_.fixOf;   // 1.22.5 : une correction automatique arretee
    const std::string fixConv = gen_.conversation;
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
        // 1.22.6 : ce que la reponse arretee a deja consomme (le service le compte) - au cumul, avec le message, et son bilan.
        if (gen_.live) {
            recordLive();
            if (!gen_.usage.empty() || !gen_.preread.empty())
                m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total() + gen_.carried.total()));
            keepBill(m, false);
        }
        gen_ = {};
        save(*c);
    } else {
        gen_ = {};
    }
    notice_ = "G\xC3\xA9n\xC3\xA9ration arr\xC3\xAAt\xC3\xA9" "e : la r\xC3\xA9ponse est gard\xC3\xA9" "e telle quelle, sans proposition.";
    // 1.22.0 : une reponse de mission arretee - l'appel est marque interrompu, ses etapes redeviennent pretes ; rien
    // n'est propose ni rejoue.
    endMissionCall(call, true, "la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e avant d'avoir propos\xC3\xA9 quoi que ce soit");
    fixAborted(fixConv, fixOf, !call.fixOf.empty() ? call.id : std::string{}, "la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 arr\xC3\xAAt\xC3\xA9" "e");   // 1.22.5
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
        XPG_TRAVAIL("ia.lecture-classeur");   // 1.22.2 : l'onglet Performances, travaux de fond
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
    ck = validate(doc->project, *p, host_.env);
    if (const auto v = verdictOf(*p)) build::merge(ck, *v);   // 1.22.5 : l'avis du logiciel, garde avec elle
    return ck;
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
    if (std::string w; !missionMayApply(p->id, &w) || !loopMayApply(*p, &w)) {   // 1.22.4 : retenue (elle tourne en rond)
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
    if (!c || !p || !doc || p->state != "proposee" || !checkInBackground(*p)) return false;   // 1.22.1 : << adresser >> aussi
    if (std::string w; !missionMayApply(p->id, &w)) return fail(w);   // 1.22.0 : pause, annulee, obsolete, autre projet
    if (std::string w; !loopMayApply(*p, &w)) return fail(w);          // 1.22.4 : retenue (elle tourne en rond)
    Env frozen;
    if (!frozenEnvironment(frozen)) return false;
    auto job = std::make_shared<ApplyJob>();
    job->conversation = c->id;
    job->proposalId = p->id;
    job->message = message;
    job->ignoringErrors = ignoringErrors;
    job->revision = doc->revision;
    job->before = [&] {
        XPG_MESURE("ia.copie-projet");   // 1.22.2 : la copie se fait sur ce fil
        return std::make_shared<const hmi::Project>(doc->project);
    }();
    job->total = 3 * p->ops.size();   // la validation du plan, celle de l'application, l'application
    job->worker = std::thread([job, prop = *p, env = std::move(frozen)] {
        XPG_TRAVAIL("ia.application");   // 1.22.2 : l'onglet Performances, travaux de fond
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

// ---- 1.22.1 : la verification de fond ------------------------------------------------------------------------------
bool Assistant::startCheck(const std::string& conversation, const Proposal& p, CheckFor purpose, std::string missionId,
                           std::vector<std::string> steps, int attempt) {
    const auto doc = host_.document ? host_.document() : nullptr;
    Env frozen;
    if (!doc || !frozenEnvironment(frozen)) return false;
    auto job = std::make_shared<CheckJob>();
    job->conversation = conversation;
    job->proposalId = p.id;
    job->mission = std::move(missionId);
    job->steps = std::move(steps);
    job->purpose = purpose;
    job->attempt = attempt;
    job->revision = doc->revision;
    job->started = lastNow_;   // 1.22.6
    auto project = [&] {
        XPG_MESURE("ia.copie-projet");   // 1.22.2 : la copie se fait sur ce fil
        return std::make_shared<const hmi::Project>(doc->project);
    }();
    job->worker = std::thread([job, project, prop = p, env = std::move(frozen)] {
        XPG_TRAVAIL("ia.verification");   // 1.22.2 : l'onglet Performances, travaux de fond
        job->result = validate(*project, prop, env);
        job->finished.store(true, std::memory_order_release);
    });
    checkJobs_.push_back(std::move(job));
    ++revision_;
    return true;
}

bool Assistant::checkingProposal(std::string_view proposalId) const {
    return std::any_of(checkJobs_.begin(), checkJobs_.end(), [&](const std::shared_ptr<CheckJob>& j) { return j && j->proposalId == proposalId; });
}

void Assistant::pollChecks() {
    for (std::size_t k = 0; k < checkJobs_.size();) {
        if (!checkJobs_[k] || !checkJobs_[k]->finished.load(std::memory_order_acquire)) {
            ++k;
            continue;
        }
        const std::shared_ptr<CheckJob> job = checkJobs_[k];
        checkJobs_.erase(checkJobs_.begin() + static_cast<std::ptrdiff_t>(k));
        if (job->worker.joinable()) job->worker.join();
        ++revision_;
        Conversation* c = nullptr;
        for (auto& conv : conversations_)
            if (conv.id == job->conversation) c = &conv;
        Proposal* prop = nullptr;
        if (c)
            for (auto& m : c->messages)
                if (m.proposal && m.proposal->id == job->proposalId) prop = &*m.proposal;
        if (!prop || prop->state != "proposee") continue;   // partie, appliquee ou refusee pendant la verification
        const auto doc = host_.document ? host_.document() : nullptr;
        // Le projet a change pendant la verification : refaite sur son etat d'aujourd'hui (trois fois au plus ; ensuite le
        // resultat est pris tel quel - la carte et l'application revalident de toute facon).
        if (doc && doc->revision != job->revision && job->attempt < 2 &&
            startCheck(job->conversation, *prop, job->purpose, job->mission, job->steps, job->attempt + 1))
            continue;
        knownChecks_[prop->id] = {job->revision, job->result};
        prop->create = job->result.create;
        prop->modify = job->result.modify;
        prop->remove = job->result.remove;
        // 1.22.5 : le processus de developpement - sa verification, enfin connue (une fois)
        if (prop->extra.has("processus")) {
            bool said = false;
            for (const auto& l : processOf(*prop)) said = said || (l.what == "verification" && l.proposal == prop->id);
            if (!said)
                addProcess(*prop, "verification",
                           "V\xC3\xA9rification : " + plural(job->result.errors.size(), "erreur", "erreurs")
                               + (job->result.warnings.empty() ? std::string{} : ", " + plural(job->result.warnings.size(), "avertissement", "avertissements")),
                           static_cast<int>(job->result.errors.size()), static_cast<int>(job->result.warnings.size()));
        }
        // 1.22.5 : son avis du logiciel attendait cette verification (une grosse proposition) : il est tire maintenant
        // (ses erreurs avec celles du logiciel - le cercle, la correction automatique).
        const bool verdictWaits = verdictOf(*prop).has_value() && !settled_.count(prop->id);
        // 1.22.4 : une grosse proposition de l'IA connectee (deja comparee a son arrivee) : ses erreurs, enfin connues
        if (job->purpose == CheckFor::Counts && prop->extra.at("cercle").flag("comparee") && !verdictWaits)
            for (std::size_t i = 0; i < c->messages.size(); ++i)
                if (c->messages[i].proposal && c->messages[i].proposal->id == job->proposalId) {
                    loopArrived(*c, static_cast<int>(i), &job->result.errors);
                    break;
                }
        if (job->purpose == CheckFor::Mission && !missionsReadOnly_.count(job->mission))
            if (mission::Mission* m = missionMut(job->mission)) missionChecked(*m, *prop, job->steps, &job->result, m->state, isoDate(now()));
        if (verdictWaits) {
            int index = -1;
            if (proposalIn(*c, job->proposalId, &index)) {
                save(*c);
                settleVerdict(*c, index, job->result);   // enregistre ; le mode Direct peut appliquer : `c` n'est plus relu
                continue;
            }
        }
        save(*c);
    }
}

// =====================================================================================================================
//  1.22.5 : L'AVIS DU LOGICIEL (AiBuild.hpp) ET LE PROCESSUS DE DEVELOPPEMENT
// ---------------------------------------------------------------------------------------------------------------------
//  LE CIRCUIT. finishLive (une proposition de l'IA connectee arrive : conversation ou mission) -> le processus (recue,
//  verifiee) -> startBuild : une copie du projet (sur ce fil), l'environnement fige, build::verdict sur un fil de fond.
//  Au tick, pollBuilds : le verdict garde avec la proposition (extra << logiciel >>), dans son processus, puis TIRE
//  (settleVerdict) quand sa validation est connue (tout de suite ; une grosse proposition : a la fin de sa verification de
//  fond, pollChecks) : ses erreurs avec celles du logiciel (build::merge) -> la mission (onChecked : les empreintes des
//  erreurs) et le cercle (AiLoop : une correction qui revient au meme point est retenue) ; des erreurs nouvelles et
//  << Corriger seule >> : une correction automatique (AutoFix) en file. runFixes (au tick, aucune reponse en cours) la
//  fait partir (launchFix) : une conversation - la proposition passe << remplacee >>, un message du LOGICIEL porte le retour
//  (build::feedbackText) et part a l'IA (startLive, gen_.fixOf) ; une mission - ses etapes en erreur (l'erreur du logiciel),
//  la proposition << remplacee >>, et sa correction (launchMission, mission::correctionPrompt + le retour du logiciel). La
//  reponse de l'IA (finishLive) : la proposition corrigee reprend le processus (linkCorrection), puis le meme chemin ; sans
//  proposition, arretee ou en erreur : la proposition d'avant revient (fixAborted). kMaxFixes corrections par demande.
// =====================================================================================================================
std::vector<Assistant::ProcessLine> Assistant::processOf(const Proposal& p) {
    std::vector<ProcessLine> out;
    const json::Value* list = p.extra.isObject() ? p.extra.find("processus") : nullptr;
    if (!list || !list->isArray()) return out;
    for (const auto& e : list->items()) {
        if (!e.isObject()) continue;
        ProcessLine l;
        l.date = e.str("date");
        l.time = e.str("heure", clockTime(l.date));
        l.what = e.str("quoi");
        l.text = e.str("texte");
        l.errors = static_cast<int>(e.integer("erreurs"));
        l.warnings = static_cast<int>(e.integer("avertissements"));
        l.proposal = e.str("proposition");
        for (const auto& f : e.at("constats").items()) {
            ProcessLine::Finding x;
            x.error = f.flag("erreur", true);
            x.text = f.str("texte");
            x.object = f.str("objet");
            x.view = f.str("vue");
            l.findings.push_back(std::move(x));
        }
        if (const std::int64_t more = e.integer("autres"); more > 0)
            l.findings.push_back({false, "\xE2\x80\xA6 et " + plural(static_cast<std::size_t>(more), "autre constat", "autres constats")
                                             + " (Pr\xC3\xA9visualiser les montre tous)", {}, {}});
        out.push_back(std::move(l));
    }
    return out;
}

std::vector<Assistant::ProcessLine> Assistant::processOf(int message) const {
    const Proposal* p = proposal(message);
    return p ? processOf(*p) : std::vector<ProcessLine>{};
}

std::optional<build::Verdict> Assistant::verdictOf(const Proposal& p) {
    const json::Value* v = p.extra.isObject() ? p.extra.find("logiciel") : nullptr;
    if (!v || !v->isObject()) return std::nullopt;
    return build::fromJson(*v);
}

std::optional<build::Verdict> Assistant::verdictOf(int message) const {
    const Proposal* p = proposal(message);
    return p ? verdictOf(*p) : std::nullopt;
}

bool Assistant::building(std::string_view proposalId) const {
    return std::any_of(buildJobs_.begin(), buildJobs_.end(), [&](const std::shared_ptr<BuildJob>& j) { return j && j->proposalId == proposalId; });
}

bool Assistant::processWorking(int message) const {
    const Proposal* p = proposal(message);
    if (!p) return false;
    if (building(p->id)) return true;
    if (generating() && (gen_.fixOf == p->id || gen_.mission.fixOf == p->id)) return true;
    return std::any_of(fixQueue_.begin(), fixQueue_.end(), [&](const AutoFix& f) { return f.proposalId == p->id; });
}

std::string Assistant::processHeadline(int message) const {
    const Proposal* p = proposal(message);
    if (!p) return {};
    if (building(p->id)) return "G\xC3\xA9n\xC3\xA9rer et Compiler v\xC3\xA9rifient la proposition sur une copie du projet\xE2\x80\xA6";
    if (generating()) {
        if (gen_.fixOf == p->id)
            return "Correction " + std::to_string(gen_.fixN) + "/" + std::to_string(kMaxFixes) + " : l'IA corrige " + fixWhat(gen_.fixWho, gen_.fixErrors) + "\xE2\x80\xA6";
        if (gen_.mission.fixOf == p->id)
            return "Correction " + std::to_string(gen_.mission.fixN) + "/" + std::to_string(kMaxFixes) + " : l'IA corrige "
                   + fixWhat(gen_.mission.fixWho, gen_.mission.fixErrors) + "\xE2\x80\xA6";
    }
    for (const auto& f : fixQueue_)
        if (f.proposalId == p->id)
            return "Correction " + std::to_string(f.n) + "/" + std::to_string(kMaxFixes) + " en attente (" + fixWhat(f.who, f.errors)
                   + ") : elle part \xC3\xA0 la fin de la r\xC3\xA9ponse en cours\xE2\x80\xA6";
    const auto lines = processOf(*p);
    return lines.empty() ? std::string{} : lines.back().time + " " + lines.back().text;
}

std::string Assistant::autoCorrecting(std::string_view missionId) const {
    if (!generating() || missionId.empty() || gen_.mission.id != missionId || gen_.mission.fixOf.empty()) return {};
    return fixLabel(gen_.mission.fixWho, gen_.mission.fixErrors);
}

void Assistant::addProcess(Proposal& p, const std::string& what, const std::string& text, int errors, int warnings, const build::Verdict* v) {
    if (!p.extra.isObject()) p.extra = json::Value::object();
    json::Value& list = p.extra["processus"];
    if (!list.isArray()) list = json::Value::array();
    const std::string when = isoDate(now());
    json::Value e = json::Value::object();
    e.set("date", when);
    e.set("heure", clockTime(when));
    e.set("quoi", what);
    e.set("texte", text);
    e.set("erreurs", errors);
    e.set("avertissements", warnings);
    e.set("proposition", p.id);
    if (v && !v->findings.empty()) {
        constexpr std::size_t kShown = 8;
        json::Value found = json::Value::array();
        for (std::size_t k = 0; k < v->findings.size() && k < kShown; ++k) {
            const build::Finding& f = v->findings[k];
            json::Value x = json::Value::object();
            x.set("erreur", f.error);
            x.set("texte", f.step + " : " + f.message + (f.where.empty() ? std::string{} : " (" + f.where + ")"));
            if (!f.objectName.empty()) x.set("objet", f.objectName);
            if (!f.view.empty()) x.set("vue", f.view);
            found.push(std::move(x));
        }
        e.set("constats", std::move(found));
        if (v->findings.size() > kShown) e.set("autres", static_cast<std::int64_t>(v->findings.size() - kShown));
    }
    list.push(std::move(e));
    // borne : une demande qui tourne longtemps garde ses 60 dernieres entrees (la premiere aussi)
    constexpr std::size_t kMaxLines = 60;
    if (list.items().size() > kMaxLines) list.items().erase(list.items().begin() + 1, list.items().end() - static_cast<std::ptrdiff_t>(kMaxLines - 1));
    ++revision_;
}

bool Assistant::startBuild(Conversation& c, int message, const Check* validated) {
    if (message < 0 || message >= static_cast<int>(c.messages.size()) || !c.messages[static_cast<std::size_t>(message)].proposal) return false;
    Proposal& p = *c.messages[static_cast<std::size_t>(message)].proposal;
    if (!settings_.buildCheck) {
        noVerdict(p, "d\xC3\xA9sactiv\xC3\xA9s (Param\xC3\xA8tres de l'Assistant IA \xE2\x80\xBA Faire v\xC3\xA9rifier par G\xC3\xA9n\xC3\xA9rer et Compiler)");
        return false;
    }
    const auto doc = host_.document ? host_.document() : nullptr;
    Env frozen;
    if (!doc) {
        noVerdict(p, "aucun projet IHM ouvert");
        return false;
    }
    if (p.ops.empty()) {
        noVerdict(p, "la proposition n'a aucune op\xC3\xA9ration");
        return false;
    }
    if (!frozenEnvironment(frozen)) {
        noVerdict(p, "les noms de l'automate reli\xC3\xA9 ne se figent pas pour un fil de fond");
        return false;
    }
    auto job = std::make_shared<BuildJob>();
    job->conversation = c.id;
    job->proposalId = p.id;
    job->key = contentKey(p);
    job->started = lastNow_;   // 1.22.6
    auto project = [&] {
        XPG_MESURE("ia.copie-projet");   // la copie se fait sur ce fil (comme la verification de fond)
        return std::make_shared<const hmi::Project>(doc->project);
    }();
    std::optional<Check> check;
    if (validated) check = *validated;
    job->worker = std::thread([job, project, prop = p, env = std::move(frozen), check = std::move(check)] {
        XPG_TRAVAIL("ia.logiciel");   // l'onglet Performances, travaux de fond
        const auto t0 = std::chrono::steady_clock::now();
        try {
            job->result = check ? build::verdict(*project, prop, env, *check) : build::verdict(*project, prop, env);
        } catch (const std::exception& e) {
            job->result = build::Verdict{};
            job->result.why = std::string("erreur pendant la v\xC3\xA9rification : ") + e.what();
        } catch (...) {
            job->result = build::Verdict{};
            job->result.why = "erreur pendant la v\xC3\xA9rification";
        }
        if (job->result.ms <= 0.0) job->result.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        job->finished.store(true, std::memory_order_release);
    });
    buildJobs_.push_back(std::move(job));
    ++revision_;
    return true;
}

void Assistant::noVerdict(Proposal& p, const std::string& why) {
    build::Verdict v;
    v.why = why;
    if (!p.extra.isObject()) p.extra = json::Value::object();
    p.extra.set("logiciel", build::toJson(v));
    ++verdictRev_;
    ++revision_;
}

void Assistant::proposalChanged(Conversation& c, int message) {
    if (message < 0 || message >= static_cast<int>(c.messages.size()) || !c.messages[static_cast<std::size_t>(message)].proposal) return;
    const Message& msg = c.messages[static_cast<std::size_t>(message)];
    Proposal& p = *c.messages[static_cast<std::size_t>(message)].proposal;
    const auto before = verdictOf(p);
    if (!before && !building(p.id)) return;   // aucun avis pour elle (une proposition d'avant la 1.22.5)
    (void)p.extra.erase("logiciel");   // l'avis d'avant ne vaut plus (la previsualisation l'oublie aussi)
    ++verdictRev_;
    directWaiting_.erase(p.id);
    // une proposition de l'IA connectee, ou dont l'avis avait tourne : redemande ; sinon (l'IA integree) : toujours << pas lances >>
    if (msg.provider.empty() && !(before && before->ran)) {
        noVerdict(p, before && !before->why.empty() ? before->why : std::string("l'avis du logiciel accompagne les propositions de l'IA connect\xC3\xA9" "e"));
        return;
    }
    if (startBuild(c, message) && p.extra.has("processus")) addProcess(p, "attente", "La proposition a chang\xC3\xA9 : G\xC3\xA9n\xC3\xA9rer et Compiler la rev\xC3\xA9rifient\xE2\x80\xA6");
}

// 1.22.5 : la correction du logiciel (fixFrom -> fixTo), dans la proposition.
namespace {
// Remplace `from` par `to` dans `s` - les mots entiers d'abord (un nom), sinon partout. Vrai : quelque chose a change.
bool replaceName(std::string& s, const std::string& from, const std::string& to) {
    if (from.empty() || s.find(from) == std::string::npos) return false;
    const auto ident = [](char ch) { return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' || static_cast<unsigned char>(ch) >= 0x80; };
    std::string out;
    bool changed = false;
    std::size_t at = 0;
    for (std::size_t k = s.find(from); k != std::string::npos; k = s.find(from, k + 1)) {
        const bool left = k == 0 || !ident(s[k - 1]) || !ident(from.front());
        const bool right = k + from.size() >= s.size() || !ident(s[k + from.size()]) || !ident(from.back());
        if (!left || !right || k < at) continue;
        out += s.substr(at, k - at) + to;
        at = k + from.size();
        changed = true;
    }
    if (!changed) {   // pas de mot entier : partout
        for (std::size_t k = s.find(from); k != std::string::npos; k = s.find(from, k + 1)) {
            if (k < at) continue;
            out += s.substr(at, k - at) + to;
            at = k + from.size();
        }
    }
    out += s.substr(at);
    if (out == s) return false;
    s = std::move(out);
    return true;
}

// 1.22.7 : UNE RETOUCHE d'une operation de la proposition (Corriger rapidement, la correction du logiciel) - gardee avec la
// forme d'avant de l'operation (Proposal::extra << retouches >>, comme la proposition l'enregistre) : Revenir la reprend.
// L'identifiant : << r >> et un numero qui ne revient pas tant qu'une retouche le porte.
void recordRetouch(Proposal& p, std::size_t op, const Operation& before, const std::string& what, const std::string& text) {
    if (!p.extra.isObject()) p.extra = json::Value::object();
    json::Value& list = p.extra["retouches"];
    if (!list.isArray()) list = json::Value::array();
    std::int64_t next = 1;
    for (const auto& r : list.items()) {
        const std::string id = r.isObject() ? r.str("id") : std::string{};
        if (id.size() > 1 && id[0] == 'r') next = std::max<std::int64_t>(next, std::atoll(id.c_str() + 1) + 1);
    }
    json::Value e = json::Value::object();
    e.set("id", "r" + std::to_string(next));
    e.set("op", static_cast<std::int64_t>(op));
    e.set("quoi", what);
    e.set("texte", text);
    const json::Value ops = operationsToJson({before});
    if (!ops.items().empty()) e.set("avant", ops.items().front());
    list.push(std::move(e));
}

// La valeur d'une propriete d'une operation (nul : absente).
std::string* propSlot(Operation& o, std::string_view key) {
    for (auto& [k, v] : o.props)
        if (lowered(k) == lowered(key)) return &v;
    return nullptr;
}
} // namespace

bool Assistant::applyBuildFix(int message, std::size_t finding, std::string* why) { return applyBuildFixes(message, {finding}, why) == 1; }

std::size_t Assistant::applyBuildFixes(int message, const std::vector<std::size_t>& findings, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return std::size_t{0};
    };
    Conversation* c = cur();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(message)].proposal)
        return fail("Pas de proposition.");
    Proposal& p = *c->messages[static_cast<std::size_t>(message)].proposal;
    if (p.state != "proposee") return fail("Cette proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    const auto v = verdictOf(p);
    std::size_t done = 0;
    std::string first, said;
    for (const auto finding : findings) {
        const auto refuse = [&](std::string w) {
            if (first.empty()) first = std::move(w);
        };
        if (!v || !v->ran || finding >= v->findings.size()) {
            refuse("Ce constat de G\xC3\xA9n\xC3\xA9rer et Compiler n'existe plus.");
            continue;
        }
        const build::Finding f = v->findings[finding];
        if (f.fixFrom.empty()) {
            refuse("Le logiciel ne propose pas de correction pour ce constat : corrige \xC3\xA0 la main, ou demande \xC3\xA0 l'IA.");
            continue;
        }
        if (f.op == Check::Fault::kNoOp || f.op >= p.ops.size()) {
            refuse("L'op\xC3\xA9ration en cause n'est pas connue : corrige \xC3\xA0 la main (Pr\xC3\xA9visualiser), ou demande \xC3\xA0 l'IA.");
            continue;
        }
        Operation& op = p.ops[f.op];
        const Operation before = op;
        bool changed = false;
        if (!f.property.empty())
            for (auto& [k, val] : op.props)
                if (k == f.property) changed = replaceName(val, f.fixFrom, f.fixTo) || changed;
        if (!changed)
            for (auto& kv : op.props) changed = replaceName(kv.second, f.fixFrom, f.fixTo) || changed;
        if (!changed) changed = replaceName(op.code, f.fixFrom, f.fixTo);
        if (!changed) changed = replaceName(op.variable, f.fixFrom, f.fixTo);
        if (!changed) {
            refuse("\xC2\xAB " + f.fixFrom + " \xC2\xBB n'est plus dans l'op\xC3\xA9ration " + std::to_string(f.op + 1) + " : rien n'est chang\xC3\xA9.");
            continue;
        }
        const std::string text = "\xC2\xAB " + f.fixFrom + " \xC2\xBB \xE2\x86\x92 \xC2\xAB " + f.fixTo + " \xC2\xBB";
        recordRetouch(p, f.op, before, "logiciel", text);   // 1.22.7 : Revenir la reprend
        addProcess(p, "correction", "Correction du logiciel pos\xC3\xA9" "e : " + text + " (op\xC3\xA9ration " + std::to_string(f.op + 1) + ")");
        said = text;
        ++done;
    }
    if (done == 0) return fail(first.empty() ? std::string("Rien \xC3\xA0 corriger.") : first);
    proposalChanged(*c, message);
    notice_ = done == 1 ? "Assistant IA : correction du logiciel pos\xC3\xA9" "e dans la proposition (" + said
                              + ") : G\xC3\xA9n\xC3\xA9rer et Compiler la rev\xC3\xA9rifient. Le projet n'a pas chang\xC3\xA9."
                        : "Assistant IA : " + std::to_string(done) + " corrections du logiciel pos\xC3\xA9" "es dans la proposition : G\xC3\xA9n\xC3\xA9rer et "
                              "Compiler la rev\xC3\xA9rifient. Le projet n'a pas chang\xC3\xA9.";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return done;
}

// ---- 1.22.7 : les retouches (Corriger rapidement) ----------------------------------------------------------------------
bool Assistant::setDeleteStrategy(int message, std::size_t op, std::string strategy, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Conversation* c = cur();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(message)].proposal)
        return fail("Pas de proposition.");
    Proposal& p = *c->messages[static_cast<std::size_t>(message)].proposal;
    if (p.state != "proposee") return fail("Cette proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    if (op >= p.ops.size() || p.ops[op].action != Operation::Action::Remove)
        return fail("L'op\xC3\xA9ration " + std::to_string(op + 1) + " n'est pas une suppression.");
    strategy = trim(std::move(strategy));
    const auto st = del::strategyFromKey(strategy);
    if (!st) return fail("Strat\xC3\xA9gie " + guillemets(strategy) + " inconnue (seul, remplacer, references).");
    Operation& o = p.ops[op];
    if (del::objectGenre(o.kind) && *st != del::Strategy::Only)
        return fail("Des objets d'une vue se suppriment seuls (strat\xC3\xA9gie seul) : leur variable et leur symbole restent.");
    const std::string* by = propSlot(o, "remplacer_par");
    if (!by) by = propSlot(o, "par");
    if (*st == del::Strategy::Replace && (!by || trim(*by).empty()))
        return fail("Remplacer demande un \xC3\xA9l\xC3\xA9ment EXISTANT du m\xC3\xAAme genre (remplacer_par) : l'Assistant n'en choisit pas un au hasard.");
    const std::string* now = propSlot(o, "strategie");
    const std::string old = now ? trim(*now) : std::string{};
    const std::string key(del::strategyKey(*st));
    const auto oldKey = del::strategyFromKey(old);
    const bool useless = lowered(old) == "inutiles" || lowered(old) == "cascade";
    if (oldKey && *oldKey == *st && !useless) return true;   // deja
    const Operation before = o;
    if (std::string* slot = propSlot(o, "strategie")) *slot = key;
    else o.props.emplace_back("strategie", key);
    if (useless && !propSlot(o, "inutiles")) o.props.emplace_back("inutiles", "oui");   // << inutiles >> garde
    const auto shown = [](const std::string& k) { return k == "references" ? std::string("r\xC3\xA9" "f\xC3\xA9rences") : k.empty() ? std::string("seul") : k; };
    recordRetouch(p, op, before, "strategie", "strat\xC3\xA9gie " + shown(lowered(old)) + " \xE2\x86\x92 " + shown(key));
    proposalChanged(*c, message);
    notice_ = "Assistant IA : strat\xC3\xA9gie " + guillemets(shown(key)) + " pos\xC3\xA9" "e pour l'op\xC3\xA9ration " + std::to_string(op + 1)
              + " (la proposition seule) : elle se rev\xC3\xA9rifie. Le projet n'a pas chang\xC3\xA9.";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::setOperationProperty(int message, std::size_t op, const std::string& key, const std::string& value, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Conversation* c = cur();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(message)].proposal)
        return fail("Pas de proposition.");
    Proposal& p = *c->messages[static_cast<std::size_t>(message)].proposal;
    if (p.state != "proposee") return fail("Cette proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    const std::string k = trim(key);
    if (op >= p.ops.size() || k.empty()) return fail("Cette op\xC3\xA9ration n'existe pas.");
    Operation& o = p.ops[op];
    if (secretField(o.action, k)) return fail("Un secret ne passe jamais par l'Assistant.");
    const std::string* now = propSlot(o, k);
    const std::string old = now ? *now : std::string{};
    if (now && trim(old) == trim(value)) return true;   // deja
    const Operation before = o;
    if (std::string* slot = propSlot(o, k)) *slot = value;
    else o.props.emplace_back(k, value);
    recordRetouch(p, op, before, "propriete", k + " : " + (trim(old).empty() ? std::string("(vide)") : trim(old)) + " \xE2\x86\x92 " + value);
    proposalChanged(*c, message);
    notice_ = "Assistant IA : " + k + " : " + value + " pour l'op\xC3\xA9ration " + std::to_string(op + 1)
              + " (la proposition seule) : elle se rev\xC3\xA9rifie. Le projet n'a pas chang\xC3\xA9.";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::undoRetouch(int message, const std::string& id, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Conversation* c = cur();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(message)].proposal)
        return fail("Pas de proposition.");
    Proposal& p = *c->messages[static_cast<std::size_t>(message)].proposal;
    if (p.state != "proposee") return fail("Cette proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    json::Value* list = p.extra.isObject() ? p.extra.find("retouches") : nullptr;
    if (!list || !list->isArray()) return fail("Cette retouche n'existe plus.");
    auto& items = list->items();
    const auto it = std::find_if(items.begin(), items.end(), [&](const json::Value& r) { return r.isObject() && r.str("id") == id; });
    if (it == items.end()) return fail("Cette retouche n'existe plus.");
    const std::int64_t at = it->integer("op", -1);
    // 1.22.8 : UNE OPERATION AJOUTEE (une unite) - elle quitte la proposition ; les indices qui la suivent reculent d'un cran
    // (les operations retirees, les autres retouches).
    if (it->str("quoi") == "ajout") {
        if (at < 0 || static_cast<std::size_t>(at) >= p.ops.size()) return fail("L'op\xC3\xA9ration ajout\xC3\xA9" "e n'existe plus : rien n'est chang\xC3\xA9.");
        const std::size_t k = static_cast<std::size_t>(at);
        const auto doc = host_.document ? host_.document() : nullptr;
        const bool fresh = doc && !p.fingerprint.empty() && proposalPrint(doc->project, p) == p.fingerprint;
        p.ops.erase(p.ops.begin() + static_cast<std::ptrdiff_t>(k));
        std::vector<std::size_t> kept;
        for (const auto x : p.excluded)
            if (x != k) kept.push_back(x > k ? x - 1 : x);
        p.excluded = std::move(kept);
        for (std::size_t r = items.size(); r-- > 0;) {
            if (!items[r].isObject()) continue;
            const std::int64_t o = items[r].integer("op", -1);
            if (o == at) items.erase(items.begin() + static_cast<std::ptrdiff_t>(r));
            else if (o > at) items[r].set("op", o - 1);
        }
        if (items.empty()) (void)p.extra.erase("retouches");
        if (fresh) p.fingerprint = proposalPrint(doc->project, p);   // l'empreinte suit (rien d'autre n'a change)
        proposalChanged(*c, message);
        notice_ = "Assistant IA : l'op\xC3\xA9ration ajout\xC3\xA9" "e quitte la proposition (le projet ne change pas).";
        if ((c = touch(*c)) != nullptr) save(*c);
        ++revision_;
        return true;
    }
    std::vector<Operation> back;
    json::Value one = json::Value::array();
    one.push(it->at("avant"));
    if (at < 0 || static_cast<std::size_t>(at) >= p.ops.size() || !operationsFromJson(one, back) || back.size() != 1)
        return fail("La forme d'avant de l'op\xC3\xA9ration ne se relit pas : rien n'est chang\xC3\xA9.");
    p.ops[static_cast<std::size_t>(at)] = std::move(back.front());
    // Elle, et celles faites depuis sur la meme operation (l'operation reprend la forme qu'elle avait avant elle).
    const auto from = static_cast<std::ptrdiff_t>(it - items.begin());
    for (std::size_t k = items.size(); k-- > static_cast<std::size_t>(from);)
        if (items[k].isObject() && items[k].integer("op", -1) == at) items.erase(items.begin() + static_cast<std::ptrdiff_t>(k));
    if (items.empty()) (void)p.extra.erase("retouches");
    proposalChanged(*c, message);
    notice_ = "Assistant IA : retouche reprise - l'op\xC3\xA9ration " + std::to_string(at + 1) + " reprend sa forme d'avant (la proposition seule).";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::addUnit(int message, const Operation& op, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Conversation* c = cur();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size()) || !c->messages[static_cast<std::size_t>(message)].proposal)
        return fail("Pas de proposition.");
    Proposal& p = *c->messages[static_cast<std::size_t>(message)].proposal;
    if (p.state != "proposee") return fail("Cette proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider.");
    if (op.action != Operation::Action::Unit || trim(op.name).empty()) return fail("Seule une unit\xC3\xA9 (creer_unite) s'ajoute ainsi.");
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return fail("Pas de projet IHM ouvert.");
    // Deja ajoutee (ou deja proposee par l'IA) : rien de plus.
    for (std::size_t i = 0; i < p.ops.size(); ++i)
        if (p.ops[i].action == Operation::Action::Unit && lowered(trim(p.ops[i].name)) == lowered(trim(op.name))) {
            if (std::find(p.excluded.begin(), p.excluded.end(), i) != p.excluded.end())
                return fail("L'op\xC3\xA9ration " + std::to_string(i + 1) + " cr\xC3\xA9" "e d\xC3\xA9j\xC3\xA0 " + trim(op.name) + " : remets-la (CORRECTIONS, Revenir).");
            return true;
        }
    // Les controles de creer_unite, sur le projet tel qu'il est (une unite deja la a l'identique : rien a ajouter).
    const UnitPlan pl = planUnit(doc->project.config, op);
    if (!pl.error.empty()) return fail("Unit\xC3\xA9 " + guillemets(trim(op.name)) + " : " + pl.error + ".");
    if (pl.same) return true;
    const bool fresh = !p.fingerprint.empty() && proposalPrint(doc->project, p) == p.fingerprint;
    p.ops.push_back(op);
    if (fresh) p.fingerprint = proposalPrint(doc->project, p);   // l'empreinte suit l'operation ajoutee (le projet n'a pas change)
    recordRetouch(p, p.ops.size() - 1, op, "ajout", unitText(pl));
    proposalChanged(*c, message);
    notice_ = "Assistant IA : " + unitText(pl) + " - ajout\xC3\xA9" "e \xC3\xA0 la proposition (le projet ne change pas) : elle se rev\xC3\xA9rifie.";
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::validationKnown(const Proposal& p, Check* out) const {
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return false;
    if (checkInBackground(p)) {   // une grosse proposition : sa verification de fond, jamais une validation sur ce fil
        const auto it = knownChecks_.find(p.id);
        if (it == knownChecks_.end()) return false;
        if (out) *out = it->second.check;
        return true;
    }
    if (out) *out = validate(doc->project, p, host_.env);
    return true;
}

void Assistant::pollBuilds() {
    for (std::size_t k = 0; k < buildJobs_.size();) {
        if (!buildJobs_[k] || !buildJobs_[k]->finished.load(std::memory_order_acquire)) {
            ++k;
            continue;
        }
        const std::shared_ptr<BuildJob> job = buildJobs_[k];
        buildJobs_.erase(buildJobs_.begin() + static_cast<std::ptrdiff_t>(k));
        if (job->worker.joinable()) job->worker.join();
        ++revision_;
        ++verdictRev_;
        Conversation* c = nullptr;
        for (auto& conv : conversations_)
            if (conv.id == job->conversation) c = &conv;
        int index = -1;
        Proposal* p = c ? proposalIn(*c, job->proposalId, &index) : nullptr;
        if (!p) continue;   // la conversation, ou la proposition, n'existe plus
        if (contentKey(*p) != job->key) continue;   // changee depuis : un autre avis est (ou sera) demande
        const build::Verdict& v = job->result;
        p->extra.set("logiciel", build::toJson(v));
        if (p->extra.has("processus"))   // le processus d'une demande a l'IA connectee (une proposition d'avant : l'avis seul)
            addProcess(*p, "logiciel", verdictLine(v), static_cast<int>(v.errors), static_cast<int>(v.warnings), &v);
        if (v.ran && v.errors + v.warnings > 0)
            notice_ = "Assistant IA : G\xC3\xA9n\xC3\xA9rer et Compiler trouvent " + findingsText(v.errors, v.warnings) + " dans la proposition "
                      + guillemets(p->summary) + ".";
        appendActivity("conversation", "Avis du logiciel sur " + guillemets(p->summary) + " : " + verdictLine(v));
        save(*c);
        if (p->state != "proposee") {   // appliquee, refusee, remplacee pendant le calcul : l'avis est garde, rien ne part
            directWaiting_.erase(p->id);
            continue;
        }
        if (Check vck; validationKnown(*p, &vck)) settleVerdict(*c, index, vck);   // sinon : a la fin de sa verification de fond
    }
}

// LE VERDICT TIRE (une fois par proposition) : ses erreurs avec celles de la validation, le cercle, la correction, le mode Direct.
void Assistant::settleVerdict(Conversation& c, int message, const Check& validation) {
    if (message < 0 || message >= static_cast<int>(c.messages.size()) || !c.messages[static_cast<std::size_t>(message)].proposal) return;
    Proposal& p = *c.messages[static_cast<std::size_t>(message)].proposal;
    const auto verdict = verdictOf(p);
    if (!verdict || settled_.count(p.id)) return;
    settled_.insert(p.id);
    const std::string pid = p.id;
    const bool direct = directWaiting_.erase(pid) > 0;
    if (p.state != "proposee") return;
    const build::Verdict& v = *verdict;
    Check merged = validation;
    build::merge(merged, v);
    const std::string when = isoDate(now());
    std::string missionId, circleText;
    bool held = false;
    if (mission::Mission* m = missionOfProposalMut(pid)) {
        missionId = m->id;
        if (!missionsReadOnly_.count(m->id)) {
            const mission::State before = m->state;
            mission::onChecked(*m, pid, merged, when);   // ses erreurs (et leur empreinte) : celles du logiciel aussi
            const mission::ProposalLink* link = m->proposal(pid);
            const std::string P = link ? "P" + std::to_string(link->order) : std::string("La proposition");
            mission::log(*m, "validation", P + " : " + verdictLine(v) + (v.findings.empty() ? std::string{} : " \xE2\x80\x94 " + v.findings.front().step + " : " + v.findings.front().message),
                         when, {}, pid);
            if (link && !link->held() && (link->state == "generee" || link->state == "validee")) {
                XPG_MESURE("ia.cercles");
                const loop::Circle circle = loop::missionCircle(*m, pid);
                if (circle.on()) {
                    loop::flag(*m, pid, circle, when);
                    notifyMission(*m, "cercle", "La mission tourne en rond : " + m->name,
                                  circle.text + ". " + P + " est retenue : choisis sur sa carte (Modifier la demande, Corriger \xC3\xA0 la main, Ignorer cette \xC3\xA9tape, "
                                  "Forcer la fin de la mission, Proposer quand m\xC3\xAAme).",
                                  "warning");
                }
            }
            if (const mission::ProposalLink* l = m->proposal(pid); l && l->held()) {
                held = true;
                circleText = l->loop;
            }
            missionTransition(*m, before);
            saveMission(*m);
        }
    } else {
        loopArrived(c, message, &merged.errors);   // une conversation : ses erreurs avec celles du logiciel (AiLoop)
        const json::Value& x = p.extra.at("cercle");
        held = !x.str("texte").empty() && x.str("choix").empty();
        circleText = x.str("texte");
    }
    if (held) {
        addProcess(p, "cercle", "Tourne en rond : " + circleText + " \xE2\x80\x94 retenue, rien ne repart : choisis sur sa carte.", static_cast<int>(v.errors),
                   static_cast<int>(v.warnings));
    } else if (v.ran && v.errors > 0 && settings_.autoCorrect && !c.messages[static_cast<std::size_t>(message)].provider.empty()) {   // l'IA connectee
        const int done = static_cast<int>(std::max<std::int64_t>(0, p.extra.integer("corrections")));
        const std::string who = errorSteps(v);
        if (done >= kMaxFixes) {
            const std::string said = std::to_string(kMaxFixes) + " corrections n'ont pas suffi : il reste " + fixWhat(who, static_cast<int>(v.errors))
                                     + ". Corrige \xC3\xA0 la main (Pr\xC3\xA9visualiser, le clic droit sur une erreur), ou modifie la demande.";
            addProcess(p, "arret", said, static_cast<int>(v.errors), static_cast<int>(v.warnings));
            notice_ = "Assistant IA : " + said;
            appendActivity("conversation", "Correction automatique arr\xC3\xAAt\xC3\xA9" "e : " + said);
        } else {
            AutoFix fix;
            fix.conversation = c.id;
            fix.proposalId = pid;
            fix.mission = missionId;
            fix.who = who;
            fix.n = done + 1;
            fix.errors = static_cast<int>(v.errors);
            if (generating() || processing())
                addProcess(p, "attente", "Correction " + std::to_string(fix.n) + " sur " + std::to_string(kMaxFixes)
                                             + " en attente : elle part \xC3\xA0 la fin de la r\xC3\xA9ponse en cours.");
            fixQueue_.push_back(std::move(fix));
        }
    }
    const bool clean = v.clean() && merged.ok();
    std::string directWhy;
    if (direct && !clean)
        directWhy = !v.ran ? "G\xC3\xA9n\xC3\xA9rer et Compiler n'ont pas pu la v\xC3\xA9rifier"
                           : v.errors > 0 ? "G\xC3\xA9n\xC3\xA9rer et Compiler y trouvent " + plural(v.errors, "erreur", "erreurs")
                                          : "elle a " + plural(merged.errors.size(), "erreur", "erreurs") + " de v\xC3\xA9rification";
    if (direct && !clean) addProcess(p, "direct", "Mode Direct : pas appliqu\xC3\xA9" "e \xE2\x80\x94 " + directWhy + ".");
    save(c);
    ++revision_;
    // LE MODE DIRECT : appliquee seulement maintenant, l'avis du logiciel propre (la conversation ouverte ; en dernier : Appliquer
    // reordonne les conversations, `c` et `p` ne sont plus relus).
    if (direct && clean && current() && current()->id == c.id) autoApply(message);
}

void Assistant::runFixes() {
    if (fixQueue_.empty() || generating() || processing()) return;
    const AutoFix fix = fixQueue_.front();
    fixQueue_.erase(fixQueue_.begin());
    std::string why;
    if (launchFix(fix, &why)) return;
    for (auto& conv : conversations_)
        if (conv.id == fix.conversation)
            if (Proposal* p = proposalIn(conv, fix.proposalId)) {
                addProcess(*p, "arret", "La correction automatique n'est pas partie" + (why.empty() ? std::string(".") : " : " + why));
                save(conv);
            }
    notice_ = "Assistant IA : la correction automatique n'est pas partie" + (why.empty() ? std::string(".") : " : " + why);
    ++revision_;
}

// UNE CORRECTION AUTOMATIQUE PART : le retour du logiciel a l'IA, la proposition << remplacee >> (elle ne s'applique plus).
bool Assistant::launchFix(const AutoFix& fix, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    if (!settings_.enabled) return fail("l'Assistant est d\xC3\xA9sactiv\xC3\xA9");
    if (!live()) return fail("l'IA connect\xC3\xA9" "e n'est plus choisie");
    if (std::string w; !ready(&w) || budgetBlocks(&w)) return fail(w);
    Conversation* c = nullptr;
    for (auto& conv : conversations_)
        if (conv.id == fix.conversation) c = &conv;
    int index = -1;
    Proposal* p = c ? proposalIn(*c, fix.proposalId, &index) : nullptr;
    if (!p) return fail("la proposition n'existe plus");
    if (p->state != "proposee") return fail("la proposition n'est plus \xC3\xA0 d\xC3\xA9" "cider");
    const auto v = verdictOf(*p);
    if (!v || v->errors == 0) return fail("plus d'erreur \xC3\xA0 corriger");
    std::string feedback = build::feedbackText(*v);
    if (Check vck; validationKnown(*p, &vck) && !vck.ok()) feedback += "\n\nLa v\xC3\xA9rification du logiciel :\n" + app::ai::feedback(vck, *p);
    const std::string n = std::to_string(fix.n) + " sur " + std::to_string(kMaxFixes);
    const std::string pid = p->id;
    if (!fix.mission.empty()) {
        // UNE MISSION : sa correction (le meme circuit que << Corriger >> : mission::correctionPrompt), avec le retour du logiciel.
        mission::Mission* m = editableMission(fix.mission, why);
        if (!m) return false;
        settleStale(*m);
        if (const std::string b = callBlocker(*m); !b.empty()) return fail(b);
        mission::ProposalLink* link = m->proposal(pid);
        if (!link) return fail("la proposition n'est plus dans la mission");
        if (link->held()) return fail("elle tourne en rond : choisis d'abord sur sa carte");
        const mission::Mission saved = *m;
        const std::string when = isoDate(now());
        const std::string P = "P" + std::to_string(link->order);
        const std::string what = fixWhat(fix.who, fix.errors);
        // ses etapes : en erreur (l'erreur du logiciel), le temps de la correction - une correction automatique est la meme
        // tentative que la proposition qu'elle remplace (AiLoop : les tentatives d'une etape)
        std::string first;
        for (const auto& sid : link->steps)
            if (mission::Step* st = m->step(sid); st && st->state == mission::StepState::AwaitingValidation) {
                (void)mission::setStepState(*m, sid, mission::StepState::Error, when, P + " : " + fixLabel(fix.who, fix.errors) + " (G\xC3\xA9n\xC3\xA9rer et Compiler, sur une copie du projet)");
                if (st->attempts > 0) --st->attempts;
                if (first.empty()) first = sid;
            }
        if (first.empty() && !link->steps.empty()) first = link->steps.front();
        link->state = "remplacee";
        mission::log(*m, "proposition", P + " remplac\xC3\xA9" "e : G\xC3\xA9n\xC3\xA9rer et Compiler y trouvent " + what + " ; correction automatique " + n + " demand\xC3\xA9" "e \xC3\xA0 l'IA.",
                     when, {}, pid);
        p->state = "remplacee";
        MissionCall call;
        call.purpose = "correction";
        call.step = first;
        call.steps = link->steps;
        call.fixOf = pid;
        call.fixN = fix.n;
        call.fixWho = fix.who;
        call.fixErrors = fix.errors;
        call.feedback = feedback;
        const std::string text = "Corrige " + P + " de la mission " + guillemets(m->name) + " : G\xC3\xA9n\xC3\xA9rer et Compiler y trouvent " + what
                                 + " (correction automatique " + n + "). Renvoie la proposition enti\xC3\xA8re corrig\xC3\xA9" "e, et dis en une phrase ce que tu as chang\xC3\xA9.\n\n"
                                 + feedback;
        const std::string missionId = m->id;
        std::vector<Pending> keep = std::move(pending_);   // les pieces jointes du prochain message restent a l'utilisateur
        pending_.clear();
        std::string w;
        const bool sent = launchMission(*m, text, std::move(call), &w);
        pending_ = std::move(keep);
        if (!sent) {
            if (mission::Mission* again = missionMut(missionId)) {
                *again = saved;
                saveMission(*again);
            }
            for (auto& conv : conversations_)
                if (Proposal* back = proposalIn(conv, pid); back && back->state == "remplacee") back->state = "proposee";
            return fail(w);
        }
        // le message de la demande : celui du LOGICIEL (pas de l'utilisateur), et la proposition qu'il corrige
        if (const mission::Mission* mm = findMission(missionId))
            for (auto& conv : conversations_)
                if (conv.id == mm->pending.conversation && conv.messages.size() >= 2) {
                    Message& asked = conv.messages[conv.messages.size() - 2];
                    if (asked.role == "utilisateur") {
                        if (!asked.extra.isObject()) asked.extra = json::Value::object();
                        asked.extra.set("logiciel", std::string("correction"));
                        asked.extra.set("proposition", pid);
                        asked.extra.set("correction", fix.n);
                    }
                    save(conv);
                }
    } else {
        // UNE CONVERSATION : le message du logiciel (le retour de Generer et Compiler), puis l'IA (le tour de l'outil le dit
        // aussi : << remplacee >>).
        const Conversation before = *c;
        p->state = "remplacee";
        Message u;
        u.id = "m" + std::to_string(c->messages.size() + 1);
        u.role = "utilisateur";
        u.date = isoDate(now());
        u.text = "G\xC3\xA9n\xC3\xA9rer et Compiler trouvent " + plural(static_cast<std::size_t>(fix.errors), "erreur nouvelle", "erreurs nouvelles") + " dans ta proposition "
                 + guillemets(p->summary) + " (correction automatique " + n + "). Corrige-la : renvoie la proposition enti\xC3\xA8re corrig\xC3\xA9" "e "
                 "(proposer_operations), et dis en une phrase ce que tu as chang\xC3\xA9.\n\n" + feedback;
        u.extra = json::Value::object();
        u.extra.set("logiciel", std::string("correction"));
        u.extra.set("proposition", pid);
        u.extra.set("correction", fix.n);
        const std::string text = u.text;
        c->messages.push_back(std::move(u));
        nextFix_ = fix;
        std::string w;
        if (!startLive(*c, text, &w)) {
            nextFix_ = {};
            *c = before;
            return fail(w);
        }
        if (Conversation* cc = touch(*c)) save(*cc);
    }
    for (auto& conv : conversations_)
        if (conv.id == fix.conversation)
            if (Proposal* back = proposalIn(conv, pid)) {
                addProcess(*back, "correction", "Correction demand\xC3\xA9" "e \xC3\xA0 l'IA (" + n + ")", fix.errors, static_cast<int>(v->warnings));
                save(conv);
            }
    notice_ = "Assistant IA : G\xC3\xA9n\xC3\xA9rer et Compiler trouvent " + fixWhat(fix.who, fix.errors) + " \xE2\x80\x94 l'IA corrige la proposition (" + n + ").";
    appendActivity("conversation", "Correction automatique " + n + " demand\xC3\xA9" "e \xC3\xA0 l'IA : " + fixLabel(fix.who, fix.errors) + ".");
    ++revision_;
    return true;
}

// LA PROPOSITION CORRIGEE ARRIVE (finishLive) : elle reprend le processus de celle qu'elle remplace (une demande, un processus).
void Assistant::linkCorrection(Conversation& c, int message, const std::string& oldId, int n, const std::string& said) {
    if (message < 0 || message >= static_cast<int>(c.messages.size()) || !c.messages[static_cast<std::size_t>(message)].proposal) return;
    Proposal& np = *c.messages[static_cast<std::size_t>(message)].proposal;
    Proposal* op = proposalIn(c, oldId);
    for (auto it = conversations_.begin(); !op && it != conversations_.end(); ++it) op = proposalIn(*it, oldId);
    if (!np.extra.isObject()) np.extra = json::Value::object();
    if (op) {
        if (const json::Value* proc = op->extra.isObject() ? op->extra.find("processus") : nullptr) np.extra.set("processus", *proc);
        if (!op->request.empty()) np.request = op->request;   // la demande de l'utilisateur, pas le message du logiciel
        if (!op->extra.isObject()) op->extra = json::Value::object();
        op->extra.set("remplaceePar", np.id);
        if (op->state == "proposee") op->state = "remplacee";   // une correction redemandee (Reessayer)
    }
    np.extra.set("corrige", oldId);
    np.extra.set("corrections", n);
    addProcess(np, "corrigee",
               "Proposition corrig\xC3\xA9" "e : " + plural(np.ops.size(), "op\xC3\xA9ration", "op\xC3\xA9rations")
                   + (said.empty() ? std::string{} : " \xE2\x80\x94 l'IA : \xC2\xAB " + said + " \xC2\xBB"));
}

// LA CORRECTION N'A PAS ABOUTI (l'IA n'a rien propose, la reponse est arretee ou en erreur) : la proposition d'avant revient
// (a decider, comme avant), et c'est dit dans son processus ; une mission : ses etapes l'attendent de nouveau.
void Assistant::fixAborted(const std::string& conversation, const std::string& oldId, const std::string& missionId, const std::string& reason) {
    if (oldId.empty()) return;
    for (auto& conv : conversations_) {
        if (!conversation.empty() && conv.id != conversation) continue;
        Proposal* p = proposalIn(conv, oldId);
        if (!p || p->state != "remplacee" || p->extra.has("remplaceePar")) continue;
        p->state = "proposee";
        addProcess(*p, "arret", "La correction n'a pas abouti" + (reason.empty() ? std::string(".") : " : " + reason + ".") + " La proposition reste \xC3\xA0 d\xC3\xA9" "cider.");
        save(conv);
    }
    if (missionId.empty() || missionsReadOnly_.count(missionId)) return;
    mission::Mission* m = missionMut(missionId);
    mission::ProposalLink* link = m ? m->proposal(oldId) : nullptr;
    if (!link || link->state != "remplacee") return;
    const std::string when = isoDate(now());
    link->state = "generee";
    const std::string P = "P" + std::to_string(link->order);
    for (const auto& sid : link->steps)
        if (mission::Step* st = m->step(sid); st && st->state == mission::StepState::Error) {
            (void)mission::setStepState(*m, sid, mission::StepState::AwaitingValidation, when, P + " attend d'\xC3\xAAtre pr\xC3\xA9visualis\xC3\xA9" "e et appliqu\xC3\xA9" "e.");
            ++st->attempts;
        }
    mission::log(*m, "proposition", P + " de nouveau \xC3\xA0 valider : la correction automatique n'a pas abouti" + (reason.empty() ? std::string(".") : " (" + reason + ")."), when, {},
                 oldId);
    saveMission(*m);
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
    XPG_MESURE("ia.empreinte");   // 1.22.2 : l'onglet Performances
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
    proposalChanged(*c, message);   // 1.22.5 : l'avis du logiciel redemande
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
    proposalChanged(*c, message);   // 1.22.5
    save(*c);
    ++revision_;
    return true;
}

// ---- 1.22.1 : les corrections (le clic droit sur une erreur) -------------------------------------------------
namespace {
bool sameName(std::string_view a, std::string_view b) {
    const auto cut = [](std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
        return s;
    };
    a = cut(a);
    b = cut(b);
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}
} // namespace

bool Assistant::chooseVariable(int message, const std::string& from, VariableChoice choice) {
    auto* c = cur();
    if (!c || message < 0 || static_cast<std::size_t>(message) >= c->messages.size()) return false;
    auto& m = c->messages[static_cast<std::size_t>(message)];
    if (!m.proposal || m.proposal->state != "proposee") return false;
    choice.from = trim(from);
    choice.use = trim(choice.use);
    if (choice.from.empty() || (choice.use.empty() && !choice.create)) return false;
    if (choice.type.empty()) choice.type = "BOOL";
    auto& list = m.proposal->choices;
    const auto it = std::find_if(list.begin(), list.end(), [&](const VariableChoice& x) { return sameName(x.from, choice.from); });
    if (it != list.end()) {
        if (*it == choice) return true;
        *it = std::move(choice);
    } else {
        list.push_back(std::move(choice));
    }
    proposalChanged(*c, message);   // 1.22.5
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::clearChoice(int message, const std::string& from) {
    auto* c = cur();
    if (!c || message < 0 || static_cast<std::size_t>(message) >= c->messages.size()) return false;
    auto& m = c->messages[static_cast<std::size_t>(message)];
    if (!m.proposal || m.proposal->state != "proposee") return false;
    auto& list = m.proposal->choices;
    const auto before = list.size();
    std::erase_if(list, [&](const VariableChoice& x) { return sameName(x.from, from); });
    if (list.size() == before) return false;
    proposalChanged(*c, message);   // 1.22.5
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

bool Assistant::excludeOps(int message, const std::vector<std::size_t>& ops, bool excluded) {
    auto* c = cur();
    if (!c || message < 0 || static_cast<std::size_t>(message) >= c->messages.size()) return false;
    auto& m = c->messages[static_cast<std::size_t>(message)];
    if (!m.proposal || m.proposal->state != "proposee" || ops.empty()) return false;
    auto& ex = m.proposal->excluded;
    bool changed = false;
    for (const auto op : ops) {
        if (op >= m.proposal->ops.size()) return false;
        const auto it = std::find(ex.begin(), ex.end(), op);
        if (excluded && it == ex.end()) {
            ex.push_back(op);
            changed = true;
        } else if (!excluded && it != ex.end()) {
            ex.erase(it);
            changed = true;
        }
    }
    if (!changed) return true;
    std::sort(ex.begin(), ex.end());
    proposalChanged(*c, message);   // 1.22.5
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    return true;
}

void Assistant::autoApply(int message) {
    // 1.22.6 : UNE PROPOSITION SENSIBLE (desactiver la securite, toucher le dernier compte ou groupe qui administre :
    // AiOperations sensitive) - le mode Direct ne l'applique JAMAIS sans clic : elle attend sur sa carte, la raison dite.
    if (const Proposal* sp = proposal(message); sp && sensitive(*sp)) {
        const std::string said = "Mode Direct : pas appliqu\xC3\xA9" "e \xE2\x80\x94 op\xC3\xA9ration sensible (la s\xC3\xA9" "curit\xC3\xA9, les comptes) : "
                                 "v\xC3\xA9rifie l'aper\xC3\xA7u, puis clique Appliquer.";
        notice_ = said;
        if (auto* c = cur())
            if (Proposal* p = proposalIn(*c, sp->id)) {
                addProcess(*p, "direct", said);
                save(*c);
            }
        return;
    }
    // 1.22.1 : une proposition lourde s'applique sur un fil de fond (sa verification comprise) ; avec des suppressions,
    // l'ecran demande d'abord (sa verification de fond dit s'il faut confirmer) - jamais une validation sur ce fil.
    if (const Proposal* p = proposal(message); p && checkInBackground(*p)) {
        const bool removes = std::any_of(p->ops.begin(), p->ops.end(), [](const Operation& op) { return op.action == Operation::Action::Remove; });
        std::string why;
        if (removes) directConfirm_ = message;
        else if (!applyInBackground(message, false, &why) && !why.empty()) notice_ = "Mode Direct : " + why;
        return;
    }
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
    // 1.22.5 : remplacee par sa correction (l'avis du logiciel) - le message suivant, du logiciel, porte ses erreurs.
    if (p.state == "remplacee")
        return {"non appliqu\xC3\xA9" "e : G\xC3\xA9n\xC3\xA9rer et Compiler y ont trouv\xC3\xA9 des erreurs (le message suivant du logiciel les donne) ; "
                "elle est remplac\xC3\xA9" "e par ta proposition corrig\xC3\xA9" "e et ne s'appliquera pas.",
                true};
    if (p.state == "appliquee") {
        switch (appliedOf(p)) {
            case Applied::Undone: return {"annul\xC3\xA9" "e (Ctrl+Z) : appliqu\xC3\xA9" "e puis annul\xC3\xA9" "e par l'utilisateur, la vue est revenue \xC3\xA0 son \xC3\xA9tat d'avant.", false};
            case Applied::Changed: return {"appliqu\xC3\xA9" "e ; la vue a chang\xC3\xA9 depuis (d'autres modifications de l'utilisateur).", false};
            default: return {"appliqu\xC3\xA9" "e au projet.", false};
        }
    }
    // Encore proposee : la validation d'aujourd'hui dit si elle peut s'appliquer.
    if (const auto doc = host_.document ? host_.document() : nullptr) {
        // 1.22.1 : une proposition lourde - sa derniere verification de fond, jamais une nouvelle validation sur ce fil
        // (celui de l'interface) ; aucune encore : dite en attente.
        std::optional<Check> known;
        if (checkInBackground(p)) {
            const auto it = knownChecks_.find(p.id);
            if (it == knownChecks_.end())
                return {"en attente : pas encore appliqu\xC3\xA9" "e (sa v\xC3\xA9rification sur le projet est en cours).", false};
            known = it->second.check;
        }
        const Check ck = known ? std::move(*known) : validate(doc->project, p, host_.env);
        // 1.22.5 : L'AVIS DU LOGICIEL (Generer et Compiler sur une copie) - ses constats, ou << en cours >>. La validation
        // garde son texte (feedback) ; le verdict s'y ajoute une fois (build::feedbackText), jamais verse dans ses erreurs.
        const auto verdict = verdictOf(p);
        std::string logiciel;
        if (verdict) {
            logiciel = build::feedbackText(*verdict);
            if (logiciel.empty() && verdict->ran) logiciel = verdict->summary() + ".";
        }
        const bool buildErrors = verdict && verdict->ran && verdict->errors > 0;
        const auto withVerdict = [&logiciel](std::string text) { return logiciel.empty() ? text : text + "\n" + logiciel; };
        // Refusee par la validation : le texte ecrit pour le modele (les erreurs, le detail des
        // genres en cause) - il corrige au tour suivant.
        if (!ck.ok()) return {withVerdict(feedback(ck, p)), true};
        if (ck.stale) return {withVerdict("en attente : pas encore appliqu\xC3\xA9" "e, et la vue a chang\xC3\xA9 depuis (elle ne s'appliquera plus telle quelle)."), false};
        if (building(p.id))
            return {"en attente : G\xC3\xA9n\xC3\xA9rer et Compiler en cours (le logiciel v\xC3\xA9rifie la proposition sur une copie du projet ; pas encore appliqu\xC3\xA9" "e).",
                    false};
        if (verdict)
            return {withVerdict("en attente : pas encore appliqu\xC3\xA9" "e (l'utilisateur ne l'a ni appliqu\xC3\xA9" "e ni refus\xC3\xA9" "e)."), buildErrors};
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
    // 1.22.6 : L'HISTORIQUE RESUME. Les derniers appels d'outil partent entiers (avec leur resultat reel), et toute proposition
    // encore a decider ; un appel plus ancien et regle (appliquee, refusee, remplacee, invalide ; un plan enregistre) part
    // RESUME dans le texte de son tour (compactCall : son libelle, ses operations comptees, les noms, son sort) - son entree
    // JSON n'est plus renvoyee a chaque demande. Les resumes avancent PAR PAQUETS de kCompactEvery appels (entre kFullCalls et
    // kFullCalls + kCompactEvery - 1 appels restent entiers) : l'historique ne change pas d'une demande a l'autre entre deux
    // paquets et se relit en cache. Le resultat d'un appel entier qui n'est pas le dernier ne dit plus << la vue a change
    // depuis >> (les propositions suivantes l'ont changee) : il ne bouge plus non plus.
    std::vector<std::size_t> calls;
    for (std::size_t i = 0; i < c.messages.size() && i < upTo; ++i)
        if (c.messages[i].tool && c.messages[i].role != "utilisateur") calls.push_back(i);
    const std::size_t lastCall = calls.empty() ? static_cast<std::size_t>(-1) : calls.back();
    const std::size_t compacted = calls.size() >= kFullCalls + kCompactEvery ? (calls.size() - kFullCalls) / kCompactEvery * kCompactEvery : 0;
    const auto keptWhole = [&](std::size_t i, const Message& m) {
        if (compacted == 0 || i >= calls[compacted]) return true;
        return m.proposal && m.proposal->state == "proposee" && m.tool && m.tool->error.empty();
    };
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
            std::string digest = m.tool && !keptWhole(i, m) ? compactCall(m) : std::string{};
            if (!digest.empty()) {   // 1.22.6 : un appel d'avant, regle : resume dans le texte du tour, sans appel ni resultat
                t.text = trim(t.text.empty() ? digest : t.text + "\n\n" + digest);
                pendingId.clear();
            } else if (m.tool) {
                t.toolUseId = m.tool->id;
                t.toolName = m.tool->name;
                t.toolInputJson = m.tool->input;
                pendingId = m.tool->id;
                if (i != lastCall && m.proposal && m.proposal->state == "appliquee" && m.tool->error.empty()) {
                    // 1.22.6 : un appel entier qui n'est pas le dernier - son sort enregistre, stable (Ctrl+Z seul le change)
                    const bool undone = appliedOf(*m.proposal) == Applied::Undone;
                    pendingText = undone ? "annul\xC3\xA9" "e (Ctrl+Z) : appliqu\xC3\xA9" "e puis annul\xC3\xA9" "e par l'utilisateur, la vue est revenue \xC3\xA0 son \xC3\xA9tat d'avant."
                                         : "appliqu\xC3\xA9" "e au projet.";
                    pendingError = false;
                } else {
                    std::tie(pendingText, pendingError) = toolResult(m);
                }
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
    const AutoFix fix = std::exchange(nextFix_, AutoFix{});           // 1.22.5 : une correction automatique (vide : aucune)
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
    // 1.22.3 : le .XPG lu comme l'automate de la seance y est dit LU (ses variables et DDT sont dans le contexte).
    if (const std::string files = attach::contextPrompt(attach::buildContext(c), host_.env.plcSource ? host_.env.plcSource() : std::string{});
        !files.empty())
        parts.dynamic += "\n" + files;
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
    // 1.22.6 : LE CACHE DE LA CONVERSATION - le contexte (il change a chaque demande) part dans le dernier tour, celui de la
    // demande ; les consignes et l'historique, devant lui, ne changent plus et se relisent en cache (AiAnthropic).
    if (!req.turns.empty() && req.turns.back().role == ProviderTurn::Role::User) req.contextTurn = static_cast<int>(req.turns.size()) - 1;
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
    const long long contextGuess = activity::requestTokens(req);   // 1.22.6 : env. les jetons envoyes (la ligne d'activite)
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
    gen_.fixOf = fix.proposalId;      // 1.22.5
    gen_.fixN = fix.n;
    gen_.fixWho = fix.who;
    gen_.fixErrors = fix.errors;
    if (ctx.view) {
        gen_.viewId = ctx.view->id;
        gen_.fingerprint = fingerprint(*ctx.view);
    }
    startActivity(contextGuess);   // 1.22.6
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
        // 1.22.6 : l'activite en direct et le cout de chaque appel (la mise en page suit quand les etapes changent)
        if (gen_.act.observe(e, lastNow_)) ++revision_;
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
                    // 1.22.6 : la somme des appels de la demande (un outil repondu sur le fil du fournisseur, puis la suite :
                    // chaque appel a ses jetons ; avant, le dernier remplacait les precedents)
                    gen_.usage = gen_.act.started() ? gen_.act.realUsage() : e.usage;
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
            case ProviderEvent::Type::Progress:   // 1.22.6 : l'activite seule (observe, plus haut)
                break;
        }
    }
}

void Assistant::finishLive(const std::string& stopReason) {
    XPG_MESURE("ia.fin-reponse");   // 1.22.2 : l'onglet Performances
    retire();
    recordLive();
    const MissionCall call = gen_.mission;   // 1.22.0 : l'appel de mission de cette reponse (vide : aucun)
    // 1.22.5 : la correction automatique de cette reponse (une proposition de conversation, ou celle d'une mission)
    AutoFix fix;
    fix.conversation = gen_.conversation;
    fix.proposalId = !call.fixOf.empty() ? call.fixOf : gen_.fixOf;
    fix.mission = !call.fixOf.empty() ? call.id : std::string{};
    fix.n = !call.fixOf.empty() ? call.fixN : gen_.fixN;
    fix.who = !call.fixOf.empty() ? call.fixWho : gen_.fixWho;
    fix.errors = !call.fixOf.empty() ? call.fixErrors : gen_.fixErrors;
    Conversation* c = generatingConversation();
    if (!c || gen_.message < 0 || gen_.message >= static_cast<int>(c->messages.size())) {
        gen_ = {};
        endMissionCall(call, true, "la conversation de la r\xC3\xA9ponse n'existe plus");
        fixAborted({}, fix.proposalId, fix.mission, "la conversation de la r\xC3\xA9ponse n'existe plus");
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
        gen_.act.finish(lastNow_, true);   // 1.22.6 : le bilan de la reponse coupee, montre avec sa reprise
        const activity::Bill cutBill = gen_.act.bill(lastNow_);
        c->messages.pop_back();
        gen_ = {};
        splitFrom_ = cut;
        nextMission_ = call;   // 1.22.0 : la reprise en parties reste l'appel de la mission (ses etapes, son lot)
        if (call.fixOf.empty()) nextFix_ = fix;   // 1.22.5 : et la correction automatique, la sienne
        std::string why;
        if (startLive(*c, request, &why)) {
            gen_.carried = spent;
            gen_.act.carry(cutBill);   // 1.22.6
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
        fixAborted({}, fix.proposalId, fix.mission, "la r\xC3\xA9ponse \xC3\xA9tait trop longue");   // 1.22.5
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
    if (!gen_.usage.empty() || !gen_.preread.empty() || !gen_.carried.empty()) {
        m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total() + gen_.carried.total()));
        // 1.22.6 : le detail, pour le bilan (le cache lu est compte dans << jetons >>, et coute dix fois moins qu'une entree)
        json::Value d = json::Value::object();
        d.set("entree", static_cast<std::int64_t>(gen_.usage.input + gen_.preread.input + gen_.carried.input));
        d.set("sortie", static_cast<std::int64_t>(gen_.usage.output + gen_.preread.output + gen_.carried.output));
        d.set("cacheLu", static_cast<std::int64_t>(gen_.usage.cacheRead + gen_.preread.cacheRead + gen_.carried.cacheRead));
        d.set("cacheEcrit", static_cast<std::int64_t>(gen_.usage.cacheWrite + gen_.preread.cacheWrite + gen_.carried.cacheWrite));
        m.extra.set("jetonsDetail", std::move(d));
    }
    keepBill(m, gen_.tool && gen_.tool->name == kOperationsTool);   // 1.22.6 : le bilan par action, garde avec la reponse
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
    bool waitBuild = false;   // 1.22.5 : Generer et Compiler tournent pour la proposition (le mode Direct l'attend)
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
                // 1.22.1 : une proposition lourde (une operation << adresser >>, 40 operations et plus) n'est plus validee ICI,
                // sur le fil de l'interface (8 s de blocage le 01/11) : sur un fil de fond (pollChecks pose ses comptes) ; celle
                // d'une mission, par la verification de la mission (missionAnswered), une seule fois.
                const bool heavy = checkInBackground(*p);
                std::optional<std::vector<std::string>> errors;   // 1.22.4 : ses erreurs, si elle est deja verifiee
                std::size_t warnings = 0;                         // 1.22.5 : le processus
                std::optional<Check> validated;                   // 1.22.5 : pour l'avis du logiciel (l'operation de chaque constat)
                if (doc && !heavy) {
                    Check ck = validate(doc->project, *p, host_.env);
                    p->create = ck.create;
                    p->modify = ck.modify;
                    p->remove = ck.remove;
                    warnings = ck.warnings.size();
                    errors = ck.errors;
                    validated = std::move(ck);
                }
                m.proposal = std::move(*p);
                if (doc && heavy && call.id.empty() && !startCheck(c->id, *m.proposal, CheckFor::Counts)) {
                    Check ck = validate(doc->project, *m.proposal, host_.env);
                    m.proposal->create = ck.create;
                    m.proposal->modify = ck.modify;
                    m.proposal->remove = ck.remove;
                    warnings = ck.warnings.size();
                    errors = ck.errors;
                    validated = std::move(ck);
                }
                // 1.22.5 : LE PROCESSUS DE DEVELOPPEMENT (l'avis du logiciel coche) - recue et verifiee ; une correction
                // automatique reprend le processus de la proposition qu'elle remplace. Puis GENERER ET COMPILER, sur un fil
                // de fond (startBuild) : son verdict arrive au tick (pollBuilds).
                if (!fix.proposalId.empty()) {
                    linkCorrection(*c, gen_.message, fix.proposalId, fix.n, firstSentence(m.text));
                } else if (settings_.buildCheck && doc) {
                    addProcess(*m.proposal, "recue", "Proposition re\xC3\xA7ue : " + plural(m.proposal->ops.size(), "op\xC3\xA9ration", "op\xC3\xA9rations"));
                    if (errors)
                        addProcess(*m.proposal, "verification",
                                   "V\xC3\xA9rification : " + plural(errors->size(), "erreur", "erreurs")
                                       + (warnings == 0 ? std::string{} : ", " + plural(warnings, "avertissement", "avertissements")),
                                   static_cast<int>(errors->size()), static_cast<int>(warnings));
                }
                waitBuild = startBuild(*c, gen_.message, validated ? &*validated : nullptr);   // sinon : un avis << pas lances >>, dit
                // 1.22.4 : NE PAS TOURNER EN ROND - une proposition de conversation comparee aux precedentes (celle d'une
                // mission : par la mission, a sa verification - missionChecked).
                if (call.id.empty()) loopArrived(*c, gen_.message, errors ? &*errors : nullptr);
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
    adviceArrived(*c, m, stopReason, !call.id.empty());   // 1.22.6 : passer en mission IA ? (le conseil, jamais la creation)
    if (m.text.empty() && !m.proposal && m.note.empty()) m.note = "R\xC3\xA9ponse vide.";
    const int index = gen_.message;
    const bool open = current() && current()->id == c->id;
    const bool hasProposal = m.proposal.has_value();
    // 1.22.0 : la reponse d'un appel de mission - le plan enregistre (et dit en clair), ou la proposition liee a ses etapes
    // et verifiee ; sans proposition ni plan : l'appel est dit interrompu ou en echec, ses etapes redeviennent pretes.
    // (Peut ajouter un message du logiciel a la conversation : `m` n'est plus utilise apres.)
    if (!call.id.empty()) missionAnswered(call, *c, index, planned, stopReason);
    // 1.22.5 : une correction automatique sans proposition corrigee - celle d'avant revient (a decider).
    const std::string pid = hasProposal ? c->messages[static_cast<std::size_t>(index)].proposal->id : std::string{};
    if (!hasProposal && !fix.proposalId.empty())
        fixAborted(c->id, fix.proposalId, fix.mission,
                   !c->messages[static_cast<std::size_t>(index)].note.empty() ? c->messages[static_cast<std::size_t>(index)].note
                                                                                : std::string("l'IA n'a pas renvoy\xC3\xA9 de proposition corrig\xC3\xA9" "e"));
    gen_ = {};
    if ((c = touch(*c)) != nullptr) save(*c);
    ++revision_;
    // 1.22.5 : LE MODE DIRECT n'applique jamais une proposition dont l'avis du logiciel n'est pas encore la (ni pas propre) :
    // elle est appliquee a l'arrivee d'un verdict propre (settleVerdict).
    if (hasProposal && open && directMode()) {
        if (waitBuild) {
            directWaiting_.insert(pid);
            if (c)
                if (Proposal* p = proposalIn(*c, pid)) {
                    addProcess(*p, "direct", "Mode Direct : appliqu\xC3\xA9" "e d\xC3\xA8s que G\xC3\xA9n\xC3\xA9rer et Compiler auront donn\xC3\xA9 un avis propre.");
                    save(*c);
                }
        } else if (settings_.buildCheck && host_.document && host_.document()) {
            std::optional<build::Verdict> v;
            if (const Proposal* p = c ? proposalIn(*c, pid) : nullptr) v = verdictOf(*p);
            notice_ = "Mode Direct : pas appliqu\xC3\xA9" "e, G\xC3\xA9n\xC3\xA9rer et Compiler n'ont pas pu la v\xC3\xA9rifier"
                      + (v && !v->why.empty() ? " (" + v->why + ")" : std::string{}) + " : Pr\xC3\xA9visualiser, puis Appliquer.";
        } else {
            autoApply(index);
        }
    }
}

void Assistant::failLive(const ProviderEvent& e) {
    retire();
    recordLive();
    const MissionCall call = gen_.mission;   // 1.22.0
    const std::string fixOf = !call.fixOf.empty() ? call.fixOf : gen_.fixOf;   // 1.22.5 : une correction automatique
    const std::string fixConv = gen_.conversation;
    Conversation* c = generatingConversation();
    if (c && gen_.message >= 0 && gen_.message < static_cast<int>(c->messages.size())) {
        Message& m = c->messages[static_cast<std::size_t>(gen_.message)];
        m.text = trim(gen_.text);
        if (!gen_.usage.empty() || !gen_.preread.empty())
            m.extra.set("jetons", static_cast<std::int64_t>(gen_.usage.total() + gen_.preread.total()));   // 1.16.0
        keepBill(m, false);   // 1.22.6
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
    // 1.22.5 : la correction automatique en echec - la proposition d'avant revient (Reessayer redemande la correction).
    fixAborted(fixConv, fixOf, !call.fixOf.empty() ? call.id : std::string{},
               "l'appel \xC3\xA0 l'IA a \xC3\xA9" "chou\xC3\xA9 (" + (e.text.empty() ? std::string("erreur inconnue") : e.text) + ")");
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
    // 1.22.5 : une CORRECTION AUTOMATIQUE (le message du logiciel) - redemandee telle quelle : la meme proposition, le meme
    // numero (la proposition d'avant est revenue a decider a l'echec : launchFix la remplace de nouveau).
    if (const Message& asked = c.messages.back(); asked.extra.isObject() && asked.extra.str("logiciel") == "correction") {
        const Message user = asked;
        const std::string convId = c.id;
        AutoFix fix;
        fix.conversation = convId;
        fix.proposalId = user.extra.str("proposition");
        fix.n = static_cast<int>(std::clamp<std::int64_t>(user.extra.integer("correction", 1), 1, kMaxFixes));
        int at = -1;
        if (Proposal* p = proposalIn(c, fix.proposalId, &at)) {
            if (const mission::Mission* m = missionOfProposal(p->id)) fix.mission = m->id;
            if (const auto v = verdictOf(*p)) {
                fix.who = errorSteps(*v);
                fix.errors = static_cast<int>(v->errors);
            }
        }
        c.messages.pop_back();
        if (launchFix(fix, &w)) {
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
    std::string ask = "Propose la suite : " + p->next;
    // 1.22.4 : la meme suite deja annoncee par une proposition precedente de la conversation - l'IA le sait (ne pas tourner
    // en rond).
    if (const Conversation* c = current()) {
        const std::string key = loop::nextKey(p->next);
        for (int i = 0; i < message && i < static_cast<int>(c->messages.size()) && !key.empty(); ++i)
            if (const auto& q = c->messages[static_cast<std::size_t>(i)].proposal;
                q && !q->next.empty() && (q->extra.at("cercle").has("suite") ? q->extra.at("cercle").str("suite") : loop::nextKey(q->next)) == key) {
                ask += " (Cette suite a d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 annonc\xC3\xA9" "e par une proposition pr\xC3\xA9" "c\xC3\xA9" "dente : ne repropose pas la m\xC3\xAAme chose ; "
                       "change d'approche, ou dis ce qui bloque.)";
                break;
            }
    }
    return ask;
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
    UsageTotals totals = usageTotals();
    // 1.22.6 : EN DIRECT - ce que la reponse en cours a deja consomme (le reel des que le service le dit, sinon une estimation
    // d'apres le texte recu, << env. >>) compte PENDANT la reponse : la conversation, la journee, le mois, le budget ; le
    // contexte de son dernier appel. Au cumul (IA/usage.json) : a sa fin, le reel seulement.
    if (gen_.live && generating()) {
        const long long live = gen_.act.started() ? gen_.act.liveTokens() : gen_.usage.total() + gen_.preread.total();
        if (gen_.conversation == (current() ? current()->id : std::string{})) st.conversation += live + gen_.carried.total();
        totals.tokensToday += live;
        totals.tokensMonth += live;
        st.estimate = gen_.act.estimating();
        if (const long long ctx = gen_.act.contextNow(); ctx > 0) st.contextUsed = ctx;
    }
    st.contextWindow = contextWindow();
    return app::ai::usageBand(settings_, st, totals, localService(), now);
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
bool Assistant::launchMission(mission::Mission& m, std::string text, MissionCall call, std::string* why, const std::vector<Attachment>* carried) {
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
    if (carried)   // 1.22.6 : celles de la conversation d'origine (la copie sur le disque est partagee, jamais dupliquee)
        for (Attachment a : *carried) {
            a.conversation = c.id;
            a.message = u.id;
            u.attachments.push_back(std::move(a));
        }
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
    // 1.22.3 : l'automate d'un .XPG joint (a cet appel, plus tot dans la conversation, ou aux pieces de la mission) - lu AVANT
    // le contexte de l'appel ; dit dans la barre de l'Assistant.
    if (std::string plcRead = ensurePlc(c, &m); !plcRead.empty()) {
        std::erase(plcRead, '*');
        std::erase(plcRead, '`');
        while (!plcRead.empty() && (plcRead.back() == '\n' || plcRead.back() == ' ')) plcRead.pop_back();
        notice_ = "Assistant IA : " + plcRead;
    }
    // L'APPEL EST ECRIT AVANT DE PARTIR : une fermeture brutale le retrouvera (interrompu ; rien n'est rejoue).
    if (call.purpose == "proposition")
        for (const auto& s : call.steps) (void)mission::setStepState(m, s, mission::StepState::Running, when, "La proposition se pr\xC3\xA9pare.");
    m.pending.active = true;
    m.pending.purpose = call.purpose;
    m.pending.conversation = c.id;
    m.pending.started = when;
    m.pending.steps = call.purpose == "correction" && call.steps.empty() ? std::vector<std::string>{call.step} : call.steps;   // 1.22.5
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
    if (call.purpose == "correction" && project) {
        std::string s = mission::correctionPrompt(m, *project, call.step);
        // 1.22.5 : une correction demandee par le logiciel - son retour (Generer et Compiler sur une copie du projet)
        if (!call.feedback.empty())
            s += "RETOUR DU LOGICIEL (G\xC3\xA9n\xC3\xA9rer et Compiler, sur une copie du projet avec ta proposition) :\n" + call.feedback + "\n";
        return s;
    }
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
    // 1.22.5 : une correction couvre les etapes de l'appel (celles de la proposition qu'elle remplace : la correction du
    // logiciel) ; celle de << Corriger >> : son etape, comme avant.
    const std::vector<std::string> steps = call.purpose == "correction" && call.steps.empty() ? std::vector<std::string>{call.step} : call.steps;
    // le lien garde avec la proposition (et dans sa conversation) : la mission, ses etapes
    p.extra.set("mission", call.id);
    {
        json::Value list = json::Value::array();
        for (const auto& s : steps) list.push(s);
        p.extra.set("etapes", list);
    }
    mission::onGenerated(*m, p, c.id, msg.id, steps, when);
    m->pending = {};
    const auto doc = host_.document ? host_.document() : nullptr;
    // 1.22.1 : UNE PROPOSITION LOURDE se verifie sur un fil de fond (pas sur celui de l'interface : le blocage du 01/11) ; la
    // mission est enregistree tout de suite (P en attente de verification), la suite (validee ou ses erreurs, obsolete, la
    // notification) vient au tick, quand la verification est finie (pollChecks -> missionChecked).
    if (doc && checkInBackground(p) && startCheck(c.id, p, CheckFor::Mission, call.id, steps)) {
        missionTransition(*m, before);
        saveMission(*m);
        return;
    }
    std::optional<Check> ck;
    // VERIFIEE SUR UNE COPIE (validate) : validee, ou ses erreurs exactes ; preparee sur un projet qui a change
    // pendant la reponse : obsolete (elle ne s'appliquera jamais, rien n'est ecrase).
    if (doc) ck = validate(doc->project, p, host_.env);
    missionChecked(*m, p, steps, ck ? &*ck : nullptr, before, when);
}

// 1.22.1 : LA SUITE D'UNE PROPOSITION DE MISSION VERIFIEE (tout de suite, ou a la fin de sa verification de fond) :
// validee ou ses erreurs exactes ; preparee sur un projet qui a change : obsolete ; la notification ; enregistree.
void Assistant::missionChecked(mission::Mission& mm, const Proposal& p, const std::vector<std::string>& steps, const Check* ck,
                               mission::State before, const std::string& when) {
    mission::Mission* m = &mm;
    bool stale = false;
    if (ck) {
        Check merged = *ck;   // 1.22.5 : l'avis du logiciel deja arrive compte aussi (ses erreurs, leur empreinte)
        if (const auto v = verdictOf(p)) build::merge(merged, *v);
        mission::onChecked(*m, p.id, merged, when);
        if (ck->stale) {
            mission::onDropped(*m, p.id, "obsolete",
                               "le projet a chang\xC3\xA9 pendant la pr\xC3\xA9paration de la proposition : elle ne s'applique pas sur un \xC3\xA9tat qui n'est "
                               "plus le sien",
                               when);
            stale = true;
        }
    }
    const mission::ProposalLink* link = m->proposal(p.id);
    const std::string P = link ? "P" + std::to_string(link->order) : std::string("La proposition");
    // 1.22.4 : NE PAS TOURNER EN ROND - comparee aux propositions precedentes de la mission (une fois, maintenant que ses
    // erreurs sont connues) : un cercle vu, elle est RETENUE (ni appliquee ni suivie d'une autre tant que l'utilisateur
    // n'a pas choisi sur sa carte).
    loop::Circle circle;
    if (!stale && link && (link->state == "generee" || link->state == "validee")) {
        XPG_MESURE("ia.cercles");
        circle = loop::missionCircle(*m, p.id);
        if (circle.on()) loop::flag(*m, p.id, circle, when);
    }
    if (stale)
        notifyMission(*m, "proposition", "Proposition obsol\xC3\xA8te : " + m->name,
                      P + " a \xC3\xA9t\xC3\xA9 pr\xC3\xA9par\xC3\xA9" "e sur un projet qui a chang\xC3\xA9 depuis : elle ne s'applique pas. Demande la suite pour en avoir une \xC3\xA0 jour.",
                      "warning");
    else if (circle.on()) {
        notifyMission(*m, "cercle", "La mission tourne en rond : " + m->name,
                      circle.text + ". " + P + " est retenue : choisis sur sa carte (Modifier la demande, Corriger \xC3\xA0 la main, Ignorer cette \xC3\xA9tape, "
                      "Forcer la fin de la mission, Proposer quand m\xC3\xAAme).",
                      "warning");
        notice_ = "Assistant IA : la mission " + guillemets(m->name) + " tourne en rond \xE2\x80\x94 " + circle.text + ". Choisis sur la carte de " + P + ".";
    } else
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

// =====================================================================================================================
//  1.22.4 : NE PAS TOURNER EN ROND (AiLoop.hpp)
// ---------------------------------------------------------------------------------------------------------------------
//  Les empreintes d'une proposition se calculent UNE fois, a son arrivee (celle d'une mission : onGenerated, onChecked ;
//  celle d'une conversation : loopArrived, gardees dans son extra << cercle >>), jamais a chaque image. La carte relit
//  seulement ce qui est garde (loopOf).
// =====================================================================================================================
void Assistant::loopArrived(Conversation& c, int message, const std::vector<std::string>* errors) {
    XPG_MESURE("ia.cercles");
    if (message < 0 || message >= static_cast<int>(c.messages.size()) || !c.messages[static_cast<std::size_t>(message)].proposal) return;
    Proposal& p = *c.messages[static_cast<std::size_t>(message)].proposal;
    loop::stamp(p, errors);
    const bool open = current() && current()->id == c.id;
    const loop::Seen now0 = loop::seenOf(p);
    std::vector<loop::Seen> earlier;
    int order = 0;
    for (int i = 0; i < message; ++i) {
        Message& msg = c.messages[static_cast<std::size_t>(i)];
        if (!msg.proposal) continue;
        // une proposition d'avant 1.22.4 (ou de l'IA integree) : ses empreintes prises une fois, gardees avec elle
        if (!msg.proposal->extra.has("cercle")) loop::stamp(*msg.proposal, nullptr);
        loop::Seen s = loop::seenOf(*msg.proposal);
        s.tag = loop::ordinalTag(++order);
        // 1.22.5 : remplacee par sa correction automatique - la meme demande : sa suite revient forcement, ce n'est pas un
        // cercle (les memes operations, les memes erreurs, si : une correction qui revient au meme point)
        if (msg.proposal->state == "remplacee") {
            s.next.clear();
            s.nextText.clear();
        }
        // appliquee puis annulee (Ctrl+Z) : dit seulement pour celle qu'elle refait (l'empreinte de la vue n'est pas
        // relue pour toutes)
        if (open && s.ops == now0.ops && msg.proposal->state == "appliquee" && appliedState(i) == Applied::Undone) s.state = "annulee";
        earlier.push_back(std::move(s));
    }
    loop::Seen now = now0;
    now.tag = loop::ordinalTag(order + 1);
    const loop::Circle circle = loop::conversationCircle(earlier, now);
    json::Value& x = p.extra["cercle"];
    x.set("comparee", true);               // comparee a son arrivee (pollChecks la reprend quand ses erreurs sont connues)
    if (!x.str("choix").empty()) return;   // deja decide : rien ne revient
    const bool before = !x.str("texte").empty();
    if (!circle.on()) {
        (void)x.erase("texte");
        (void)x.erase("signes");
        (void)x.erase("choix");
        return;
    }
    x.set("texte", circle.text);
    json::Value keys = json::Value::array();
    for (const auto& k : circle.keys()) keys.push(k);
    x.set("signes", std::move(keys));
    x.set("choix", std::string{});
    if (!before) {
        notice_ = "Assistant IA : cette proposition tourne en rond \xE2\x80\x94 " + circle.text + ". Choisis sur sa carte.";
        appendActivity("cercle", "Proposition retenue (elle tourne en rond) : " + circle.text);
    }
}

bool Assistant::loopMayApply(const Proposal& p, std::string* why) const {
    if (missionOfProposal(p.id)) return true;   // une proposition de mission : mission::mayApply le dit
    const json::Value& x = p.extra.at("cercle");
    if (x.str("texte").empty() || !x.str("choix").empty()) return true;
    if (why)
        *why = "Elle tourne en rond. " + x.str("texte") + ". Choisis d'abord sur sa carte : Proposer quand m\xC3\xAAme, Corriger \xC3\xA0 la main ou "
               "Modifier la demande.";
    return false;
}

Assistant::LoopInfo Assistant::loopOf(int message) const {
    LoopInfo out;
    const Proposal* p = proposal(message);
    if (!p) return out;
    if (const mission::Mission* m = missionOfProposal(p->id)) {
        const mission::ProposalLink* l = m->proposal(p->id);
        if (!l || l->loop.empty()) return out;
        out.text = l->loop;
        out.signs = l->loopSigns;
        out.choice = l->loopChoice;
        out.mission = m->id;
        out.steps = l->loopSteps.empty() ? l->steps : l->loopSteps;
        out.tag = "P" + std::to_string(l->order);
        out.held = l->held() && p->state == "proposee";
        return out;
    }
    const json::Value& x = p->extra.at("cercle");
    out.text = x.str("texte");
    if (out.text.empty()) return out;
    for (const auto& k : x.at("signes").items())
        if (k.isString()) out.signs.push_back(k.asString());
    out.choice = x.str("choix");
    out.held = out.choice.empty() && p->state == "proposee";
    return out;
}

bool Assistant::chooseLoop(int message, LoopChoice choice, std::string* why, std::string* request) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    Conversation* c = cur();
    const Proposal* p = proposal(message);
    if (!c || !p) return fail("Pas de proposition.");
    const LoopInfo info = loopOf(message);
    if (!info.held) return fail("Cette proposition n'attend pas de choix : elle ne tourne pas en rond, ou c'est d\xC3\xA9j\xC3\xA0 d\xC3\xA9" "cid\xC3\xA9.");
    const std::string key = choice == LoopChoice::ProposeAnyway ? "quand_meme"
                            : choice == LoopChoice::ByHand      ? "a_la_main"
                            : choice == LoopChoice::EditRequest ? "demande_modifiee"
                            : choice == LoopChoice::SkipStep    ? "etape_ignoree"
                                                                : "fin_forcee";
    const std::string pid = p->id;
    const std::string when = isoDate(now());
    // ---- une proposition de conversation : le choix est garde avec elle ----
    if (info.mission.empty()) {
        if (choice == LoopChoice::SkipStep || choice == LoopChoice::ForceFinish)
            return fail("Ce choix vaut pour une proposition de mission : celle-ci est une proposition de la conversation.");
        Proposal& mp = *c->messages[static_cast<std::size_t>(message)].proposal;
        mp.extra["cercle"].set("choix", key);
        if (request && choice == LoopChoice::EditRequest) *request = mp.request;
        notice_ = "Assistant IA : " + std::string(loop::choiceLabel(key)) + " \xE2\x80\x94 la proposition n'est plus retenue.";
        appendActivity("cercle", std::string(loop::choiceLabel(key)) + " : " + info.text);
        save(*c);
        ++revision_;
        return true;
    }
    // ---- une proposition de mission ----
    const std::string id = info.mission;
    if (choice == LoopChoice::ForceFinish && !canForceFinish(id, why)) return false;
    mission::Mission* m = editableMission(id, why);
    if (!m) return false;
    if (!loop::choose(*m, pid, key, when, why)) return false;
    const mission::ProposalLink* link = m->proposal(pid);
    const std::string P = link ? "P" + std::to_string(link->order) : info.tag;
    const std::vector<std::string> linkSteps = link ? link->steps : std::vector<std::string>{};
    saveMission(*m);
    // Refuser la proposition retenue (Modifier la demande, Ignorer cette etape) : ses etapes redeviennent pretes.
    const auto drop = [&](const std::string& reason) {
        c->messages[static_cast<std::size_t>(message)].proposal->state = "refusee";
        save(*c);
        missionDropped(pid, "refusee", reason);
    };
    switch (choice) {
        case LoopChoice::ProposeAnyway:
            if (const mission::Mission* mm = findMission(id))
                notifyMission(*mm, "proposition", "Proposition disponible : " + mm->name,
                              P + " propos\xC3\xA9" "e quand m\xC3\xAAme (elle tournait en rond) : \xC3\xA0 pr\xC3\xA9visualiser et appliquer.", "info");
            notice_ = "Assistant IA : " + P + " propos\xC3\xA9" "e quand m\xC3\xAAme \xE2\x80\x94 pr\xC3\xA9visualise-la, puis applique-la ou refuse-la.";
            break;
        case LoopChoice::ByHand:
            notice_ = "Assistant IA : " + P + " \xC3\xA0 corriger \xC3\xA0 la main \xE2\x80\x94 la pr\xC3\xA9visualisation la montre ; applique-la ensuite.";
            break;
        case LoopChoice::EditRequest: {
            drop("elle tournait en rond : la demande est \xC3\xA0 modifier");
            const std::vector<std::string> steps = info.steps.empty() ? linkSteps : info.steps;
            Steer st;
            st.mission = id;
            st.conversation = c->id;
            st.steps = steps;
            st.prefix = "Pour " + (steps.empty() ? std::string("la mission") : joined(steps, ", ")) + " : ";
            steer_ = st;
            if (request) *request = st.prefix;
            notice_ = "Assistant IA : " + P + " refus\xC3\xA9" "e (elle tournait en rond). \xC3\x89" "cris ta consigne apr\xC3\xA8s " + guillemets(trim(st.prefix))
                      + ", puis Entr\xC3\xA9" "e : la mission repart avec elle.";
            break;
        }
        case LoopChoice::SkipStep: {
            drop("elle tournait en rond : \xC3\xA9tape ignor\xC3\xA9" "e");
            std::vector<std::string> skipped, failed;
            for (const auto& sid : info.steps.empty() ? linkSteps : info.steps) {
                mission::PlanEdit e;
                e.kind = mission::PlanEdit::Kind::Skip;
                e.step = sid;
                std::string w;
                if (editMissionPlan(id, e, &w)) skipped.push_back(sid);
                else failed.push_back(sid + " (" + w + ")");
            }
            notice_ = "Assistant IA : " + P + " refus\xC3\xA9" "e ; " + (skipped.empty() ? std::string("aucune \xC3\xA9tape ignor\xC3\xA9" "e")
                                                                                       : joined(skipped, ", ") + (skipped.size() > 1 ? " ignor\xC3\xA9" "es" : " ignor\xC3\xA9" "e"))
                      + (failed.empty() ? std::string(".") : " ; pas ignor\xC3\xA9" "e(s) : " + joined(failed, ", ") + ".");
            break;
        }
        case LoopChoice::ForceFinish:
            ++revision_;
            return forceFinishMission(id, why);
    }
    ++revision_;
    return true;
}

// LA CONSIGNE apres Modifier la demande (une mission) : gardee dans la mission, puis la suite repart avec elle.
bool Assistant::steerMission(const std::string& text, std::string* why) {
    if (!steer_) return false;
    const Steer st = *steer_;
    const std::string head = trim(st.prefix);
    const std::string consigne = trim(text.substr(std::min(text.size(), head.size())));
    if (consigne.empty()) {
        if (why) *why = "\xC3\x89" "cris ce que l'IA doit faire autrement apr\xC3\xA8s " + guillemets(head) + ", puis Entr\xC3\xA9" "e.";
        return false;   // la consigne reste attendue
    }
    steer_.reset();
    mission::Mission* m = editableMission(st.mission, why);
    if (!m) return false;
    (void)loop::addConsigne(*m, st.steps, consigne, isoDate(now()));
    saveMission(*m);
    return continueMissionWith(st.mission, consigne, why);
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
    if (adviceDeclined(*c)) return false;   // 1.22.6 : << Continuer ici >> choisi pour cette conversation
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

// =====================================================================================================================
//  1.22.6 : DE LA CONVERSATION A LA MISSION IA
// ---------------------------------------------------------------------------------------------------------------------
//  Une grosse demande faite en conversation devient une mission : a la demande de l'utilisateur (un message envoye, << Envoyer
//  en mission >>), ou sur le conseil du logiciel (adviseMission, avant l'envoi ; adviceArrived, a l'arrivee) et de l'IA (la
//  regle 22 : mission_conseillee, [MISSION_CONSEILLEE: ...]). Le conseil n'est qu'une carte : jamais une mission sans le clic.
// =====================================================================================================================
bool Assistant::adviseMission(std::string_view request, MissionAdvice* out) const {
    if (!settings_.enabled || !live()) return false;
    const Conversation* c = current();
    if (c && (adviceDeclined(*c) || missionRunningIn(missions_, c->id))) return false;
    const std::string text = trim(std::string(request));
    if (text.empty() || text.back() == '?') return false;   // une question n'est jamais une mission
    const std::string low = lowered(text);
    std::vector<std::string> reasons;
    bool primary = false;
    // 1. elle enumere des etapes ou des lots, une liste, un enchainement
    if (const auto steps = stepTokens(text); steps.size() >= 3) {
        reasons.push_back("elle \xC3\xA9num\xC3\xA8re " + std::to_string(steps.size()) + " \xC3\xA9tapes (" + joined(steps, ", ") + ")");
        primary = true;
    }
    {
        std::vector<std::string> lots;
        for (std::size_t at = low.find("lot "); at != std::string::npos; at = low.find("lot ", at + 4)) {
            if (at > 0 && alnum(low[at - 1])) continue;   // << pilot 3 >> n'est pas un lot
            std::size_t j = at + 4;
            std::string n;
            while (j < low.size() && std::isdigit(static_cast<unsigned char>(low[j]))) n += low[j++];
            if (!n.empty() && std::find(lots.begin(), lots.end(), n) == lots.end()) lots.push_back(n);
        }
        if (lots.size() >= 3) {
            reasons.push_back("elle \xC3\xA9num\xC3\xA8re " + std::to_string(lots.size()) + " lots");
            primary = true;
        }
    }
    {
        std::size_t items = 0;
        std::size_t from = 0;
        while (from <= text.size()) {
            const std::size_t end = std::min(text.find('\n', from), text.size());
            std::string line = trim(text.substr(from, end - from));
            std::size_t k = 0;
            while (k < line.size() && std::isdigit(static_cast<unsigned char>(line[k]))) ++k;
            if (line.rfind("- ", 0) == 0 || line.rfind("* ", 0) == 0 || line.rfind("\xE2\x80\xA2", 0) == 0
                || (k > 0 && k + 1 < line.size() && (line[k] == '.' || line[k] == ')') && line[k + 1] == ' '))
                ++items;
            from = end + 1;
        }
        if (items >= 3) {
            reasons.push_back("elle liste " + std::to_string(items) + " \xC3\xA9l\xC3\xA9ments");
            primary = true;
        }
    }
    {
        std::size_t thens = 0;
        for (const char* w : {"puis", "ensuite", "enfin"})
            for (std::size_t at = low.find(w); at != std::string::npos; at = low.find(w, at + 1))
                if ((at == 0 || !alnum(low[at - 1])) && (at + std::strlen(w) >= low.size() || !alnum(low[at + std::strlen(w)]))) ++thens;
        if (thens >= 2) {
            reasons.push_back("elle encha\xC3\xAEne plusieurs \xC3\xA9tapes (\xC2\xAB puis \xC2\xBB)");
            primary = true;
        }
    }
    // 2. la suite d'une conversation qui en a deja annonce plusieurs
    long long spent = 0;
    bool cut = false;
    if (c) {
        std::size_t nexts = 0;
        for (const auto& m : c->messages) {
            if (m.proposal && !m.proposal->next.empty()) ++nexts;
            spent += m.extra.integer("jetons");
        }
        for (auto m = c->messages.rbegin(); m != c->messages.rend(); ++m)
            if (m->role == "assistant" && !(m->extra.isObject() && m->extra.has("logiciel"))) {
                cut = m->note.find("longueur d'une r\xC3\xA9ponse") != std::string::npos;
                break;
            }
        if (nexts >= 2 && (low.rfind("propose la suite", 0) == 0 || low.find("d\xC3\xA9j\xC3\xA0 \xC3\xA9t\xC3\xA9 annonc") != std::string::npos)) {
            reasons.push_back("la conversation a d\xC3\xA9j\xC3\xA0 annonc\xC3\xA9 une suite " + std::to_string(nexts) + " fois");
            primary = true;
        }
    }
    // 3. aggravants : la derniere reponse coupee, une conversation deja lourde - avec une demande complexe
    std::vector<std::string> heavy;
    if (cut) heavy.push_back("la derni\xC3\xA8re r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e par la longueur");
    if (spent >= kAdviceTokens) heavy.push_back("la conversation a d\xC3\xA9j\xC3\xA0 consomm\xC3\xA9 " + tokensText(spent) + " jetons");
    if (!primary && (heavy.empty() || (!mission::looksComplex(text) && countWords(text) < 40))) return false;
    for (auto& h : heavy) reasons.push_back(std::move(h));
    if (out) {
        out->reason = joined(reasons, " ; ");
        out->source = "logiciel";
        out->message = -1;
    }
    return true;
}

void Assistant::adviceArrived(Conversation& c, Message& m, const std::string& stopReason, bool missionCall) {
    std::string reason;
    // l'IA : le champ de proposer_operations...
    if (m.tool && m.tool->name == kOperationsTool) {
        json::Value in;
        if (json::parse(m.tool->input, in) && in.isObject()) reason = trim(in.str("mission_conseillee"));
    }
    // ... ou la ligne [MISSION_CONSEILLEE: la raison], retiree du texte montre (et renvoye)
    if (const auto at = m.text.find("[MISSION_CONSEILLEE"); at != std::string::npos) {
        const auto end = m.text.find(']', at);
        const std::string said = m.text.substr(at, end == std::string::npos ? std::string::npos : end - at + 1);
        const auto colon = said.find(':');
        std::string r = colon == std::string::npos ? std::string{} : trim(said.substr(colon + 1, said.size() - colon - 1 - (said.back() == ']' ? 1 : 0)));
        if (reason.empty()) reason = r.empty() ? std::string("l'IA conseille une mission IA pour cette demande") : r;
        m.text = trim(m.text.erase(at, said.size()));
    }
    std::string source = reason.empty() ? "logiciel" : "ia";
    if (reason.empty()) {
        // le logiciel : une reponse coupee par la longueur, ou la suite annoncee une fois de trop
        if (stopReason == "max_tokens" && !m.proposal) {
            reason = "la r\xC3\xA9ponse a \xC3\xA9t\xC3\xA9 coup\xC3\xA9" "e par la longueur d'une r\xC3\xA9ponse";
        } else if (m.proposal && !m.proposal->next.empty()) {
            std::size_t nexts = 0;
            for (const auto& x : c.messages)
                if (x.proposal && !x.proposal->next.empty()) ++nexts;
            if (nexts >= 3) reason = "la suite de cette demande est annonc\xC3\xA9" "e pour la " + std::to_string(nexts) + "e fois";
        }
    }
    // jamais pendant une mission (le marqueur est seulement retire), ni apres << Continuer ici >>
    if (reason.empty() || missionCall || !live() || adviceDeclined(c) || missionRunningIn(missions_, c.id)) return;
    reason = clipped(trim(reason), 300);
    json::Value a = json::Value::object();
    a.set("raison", reason);
    a.set("source", source);
    if (!m.extra.isObject()) m.extra = json::Value::object();
    m.extra.set("missionConseillee", std::move(a));
}

std::optional<Assistant::MissionAdvice> Assistant::pendingAdvice() const {
    const Conversation* c = current();
    if (!c || c->messages.empty() || !live() || adviceDeclined(*c) || missionRunningIn(missions_, c->id)) return std::nullopt;
    if (generating() && gen_.conversation == c->id) return std::nullopt;
    const Message& last = c->messages.back();
    const json::Value* a = last.role == "assistant" && last.extra.isObject() ? last.extra.find("missionConseillee") : nullptr;
    if (!a || !a->isObject() || a->flag("traite")) return std::nullopt;
    MissionAdvice out;
    out.reason = a->str("raison");
    out.source = a->str("source");
    for (int i = static_cast<int>(c->messages.size()) - 2; i >= 0; --i) {
        const Message& u = c->messages[static_cast<std::size_t>(i)];
        if (u.role == "utilisateur" && !(u.extra.isObject() && u.extra.has("logiciel"))) {
            out.message = i;
            break;
        }
    }
    if (out.message < 0) return std::nullopt;
    return out;
}

void Assistant::declineMissionAdvice() {
    Conversation* c = cur();
    if (!c) return;
    if (!c->extra.isObject()) c->extra = json::Value::object();
    c->extra.set("missionConseil", "ici");
    if (!c->messages.empty())
        if (json::Value* a = c->messages.back().extra.isObject() ? c->messages.back().extra.find("missionConseillee") : nullptr; a && a->isObject())
            a->set("traite", true);
    save(*c);
    ++revision_;
}

bool Assistant::missionAdviceDeclined() const {
    const Conversation* c = current();
    return c && adviceDeclined(*c);
}

std::string Assistant::conversationDigest(const Conversation& c, std::size_t upTo) const {
    std::vector<std::string> applied;
    std::size_t refused = 0, replaced = 0, waiting = 0, undone = 0, nexts = 0;
    std::string lastNext;
    for (std::size_t i = 0; i < c.messages.size() && i < upTo; ++i) {
        if (!c.messages[i].proposal) continue;
        const Proposal& p = *c.messages[i].proposal;
        if (!p.next.empty()) {
            ++nexts;
            lastNext = p.next;
        }
        if (p.state == "appliquee") {
            if (appliedOf(p) == Applied::Undone) ++undone;
            else applied.push_back(guillemets(p.summary) + " : " + opsDigest(p, 240));
        } else if (p.state == "refusee") {
            ++refused;
        } else if (p.state == "remplacee") {
            ++replaced;
        } else {
            ++waiting;
        }
    }
    std::vector<std::string> files;
    const std::string plc = host_.env.plcSource ? host_.env.plcSource() : std::string{};
    for (const auto& f : attach::buildContext(c).files)
        if (!f.attachment.removed) files.push_back(f.attachment.name + (!plc.empty() && f.attachment.name == plc ? " (l'automate lu)" : ""));
    if (applied.empty() && nexts == 0 && files.empty() && refused + replaced + waiting + undone == 0) return {};
    std::string s = "CONTEXTE DE LA CONVERSATION " + guillemets(c.title)
                    + " (d'o\xC3\xB9 vient cette mission ; ce qui y est d\xC3\xA9j\xC3\xA0 fait, d'apr\xC3\xA8s le logiciel - le projet ouvert fait foi) :\n";
    if (!applied.empty()) {
        s += "- Propositions appliqu\xC3\xA9" "es (" + std::to_string(applied.size()) + ") :\n";
        const std::size_t from = applied.size() > 8 ? applied.size() - 8 : 0;
        if (from > 0) s += "  - ... et " + std::to_string(from) + " plus anciennes\n";
        for (std::size_t i = from; i < applied.size(); ++i) s += "  - " + applied[i] + "\n";
    }
    if (refused + replaced + waiting + undone > 0) {
        std::vector<std::string> other;
        if (refused) other.push_back(plural(refused, "refus\xC3\xA9" "e", "refus\xC3\xA9" "es"));
        if (replaced) other.push_back(plural(replaced, "remplac\xC3\xA9" "e par sa correction", "remplac\xC3\xA9" "es par leur correction"));
        if (undone) other.push_back(plural(undone, "annul\xC3\xA9" "e (Ctrl+Z)", "annul\xC3\xA9" "es (Ctrl+Z)"));
        if (waiting) other.push_back(plural(waiting, "jamais appliqu\xC3\xA9" "e", "jamais appliqu\xC3\xA9" "es"));
        s += "- Autres propositions (rien d'elles n'est dans le projet) : " + joined(other, ", ") + ".\n";
    }
    if (nexts > 0)
        s += "- Suite annonc\xC3\xA9" "e" + (nexts > 1 ? " (" + std::to_string(nexts) + " fois)" : std::string{}) + " : " + guillemets(lastNext) + "\n";
    if (!files.empty()) s += "- Pi\xC3\xA8" "ces jointes reprises par la mission : " + joined(files, ", ") + ".\n";
    return clipped(s, 4000);
}

bool Assistant::createMissionFromMessage(int message, std::string* why) {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const Conversation* c = current();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size())) return fail("Ce message n'existe plus.");
    const Message& m = c->messages[static_cast<std::size_t>(message)];
    if (m.role != "utilisateur" || (m.extra.isObject() && m.extra.has("logiciel")))
        return fail("Seul un message que tu as envoy\xC3\xA9 devient une mission.");
    if (m.extra.isObject() && m.extra.has("mission")) return fail("Ce message fait d\xC3\xA9j\xC3\xA0 partie d'une mission.");
    const std::string text = trim(m.text);
    if (text.empty()) return fail("Ce message n'a pas de texte : \xC3\xA9" "cris ce que la mission doit construire.");
    return createMissionWith(text, message, why);
}

bool Assistant::sendAsMission(std::string text, std::string* why) {
    text = trim(std::move(text));
    const Conversation* c = current();
    if (!c || c->messages.empty()) return createMission(std::move(text), why);   // une conversation vide : la mission y reste
    return createMissionWith(std::move(text), -1, why);
}

bool Assistant::createMissionWith(std::string request, int message, std::string* why) {
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
    if (processing()) return fail("Les pi\xC3\xA8" "ces jointes sont en cours de traitement.");
    std::string w;
    if (!ready(&w)) return fail(w);
    if (toolsInText())
        return fail("Ce mod\xC3\xA8le re\xC3\xA7oit les propositions en texte : il ne peut pas appeler l'outil du plan d'une mission. Choisis "
                    "\xC2\xAB Outil \xC2\xBB (l'appel d'outil) dans les Param\xC3\xA8tres de l'Assistant IA.");
    const auto doc = host_.document ? host_.document() : nullptr;
    if (!doc) return fail("Aucun projet n'est ouvert : une mission travaille sur le projet IHM ouvert.");
    const Conversation* src = current();
    if (!src) return createMission(std::move(request), why);
    const Conversation source = *src;   // une copie : la conversation neuve deplace la liste
    const std::size_t upTo = message >= 0 ? static_cast<std::size_t>(message) : source.messages.size();
    // Les pieces : celles du message, puis celles de la conversation (le .XPG lu, les classeurs), sans doublon ni retiree.
    std::vector<Attachment> carried;
    const auto carry = [&carried](const Attachment& a) {
        if (a.removed || a.id.empty()) return;
        if (std::any_of(carried.begin(), carried.end(), [&a](const Attachment& x) { return x.id == a.id; })) return;
        carried.push_back(a);
    };
    if (message >= 0)
        for (const auto& a : source.messages[static_cast<std::size_t>(message)].attachments) carry(a);
    for (const auto& f : attach::buildContext(source).files) carry(f.attachment);
    const std::string digest = conversationDigest(source, upTo);
    const std::string when = isoDate(now());
    newConversation();
    const Conversation* fresh = cur();
    if (!fresh || fresh->id == source.id) return fail("Aucune conversation pour la mission.");
    const std::string convId = fresh->id;
    mission::Mission made = mission::make(request, convId, when);
    made.project.name = host_.projectName ? host_.projectName() : std::string{};
    made.project.path = host_.projectPath ? host_.projectPath() : std::string{};
    made.project.id = openProjectId();
    made.provider = settings_.provider;
    made.model = trim(settings_.model);
    for (const auto& a : carried) made.attachments.push_back(a.id);
    for (const auto& p : pending_)
        if (!p.attachment.id.empty()) made.attachments.push_back(p.attachment.id);
    mission::log(made, "creation", "Cr\xC3\xA9\xC3\xA9" "e depuis la conversation " + guillemets(source.title) + ".", when);
    const std::string id = made.id;
    missions_.insert(missions_.begin(), std::move(made));
    MissionCall call;
    call.purpose = "plan";
    if (!launchMission(missions_.front(), request + (digest.empty() ? std::string{} : "\n\n" + digest), std::move(call), why, &carried)) {
        // la demande n'est pas partie : ni mission, ni conversation neuve ; la conversation d'origine revient
        missions_.erase(std::remove_if(missions_.begin(), missions_.end(), [&id](const mission::Mission& x) { return x.id == id; }), missions_.end());
        (void)mission::remove(id);
        ++missionsRev_;
        (void)remove(convId);
        (void)select(source.id);
        ++revision_;
        return false;
    }
    const mission::Mission* mm = findMission(id);
    const std::string name = mm ? mm->name : std::string("Mission");
    // LA CONVERSATION D'ORIGINE le dit : << Mission << X >> creee - Ouvrir >> (un clic ouvre son tableau de bord).
    if (const int si = indexOf(source.id); si >= 0) {
        Conversation& sc = conversations_[static_cast<std::size_t>(si)];
        const int ci = indexOf(convId);
        const std::string title = ci >= 0 ? conversations_[static_cast<std::size_t>(ci)].title : std::string{};
        if (message >= 0 && message < static_cast<int>(sc.messages.size())) {
            Message& um = sc.messages[static_cast<std::size_t>(message)];
            if (!um.extra.isObject()) um.extra = json::Value::object();
            um.extra.set("missionCreee", id);
            for (std::size_t k = static_cast<std::size_t>(message) + 1; k < sc.messages.size(); ++k)   // le conseil est suivi
                if (json::Value* a = sc.messages[k].extra.isObject() ? sc.messages[k].extra.find("missionConseillee") : nullptr; a && a->isObject())
                    a->set("traite", true);
        }
        Message link;
        link.id = "m" + std::to_string(sc.messages.size() + 1);
        link.role = "assistant";
        link.state = "fini";
        link.date = when;
        link.text = "Mission " + guillemets(name) + " cr\xC3\xA9\xC3\xA9" "e \xE2\x80\x94 **Ouvrir**\n\nElle continue dans la conversation " + guillemets(title)
                    + " : l'IA analyse le projet et pr\xC3\xA9pare le plan (rien n'est modifi\xC3\xA9 avant ta validation). Un clic ici ouvre son tableau de bord.";
        link.extra = json::Value::object();
        link.extra.set("logiciel", "mission-creee");
        link.extra.set("mission", id);
        sc.messages.push_back(std::move(link));
        save(sc);
    }
    if (mm) {
        notifyMission(*mm, "creee", "Mission cr\xC3\xA9\xC3\xA9" "e : " + mm->name,
                      "Depuis la conversation " + guillemets(source.title) + " : l'IA analyse le projet r\xC3\xA9" "el et pr\xC3\xA9pare le plan ; rien n'est "
                      "modifi\xC3\xA9 avant ta validation.",
                      "info");
        notice_ = "Assistant IA : mission " + guillemets(mm->name) + " cr\xC3\xA9\xC3\xA9" "e depuis la conversation - l'IA analyse le projet et pr\xC3\xA9pare le plan.";
    }
    appendActivity("mission", "Mission cr\xC3\xA9\xC3\xA9" "e depuis une conversation : " + request);
    ++revision_;
    return true;
}

bool Assistant::continueMission(std::string_view idv, std::string* why) { return continueMissionWith(idv, {}, why); }

// 1.22.4 : `consigne` (apres Modifier la demande sur une proposition qui tournait en rond) - dite dans la demande ; la
// consigne est aussi gardee dans la mission (loop::addConsigne) et reprise par la consigne du lot (loop::triedText).
bool Assistant::continueMissionWith(std::string_view idv, const std::string& consigne, std::string* why) {
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
    std::string text = "Propose la suite de la mission " + guillemets(m->name) + " : " + stepsLine(*m, batch.steps) + ".";
    if (!consigne.empty()) text += " Consigne : " + consigne;
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
    job->copy = [&] {
        XPG_MESURE("ia.copie-projet");   // 1.22.2 : la copie se fait sur ce fil
        return std::make_shared<const hmi::Project>(doc->project);
    }();
    Env frozen;
    if (frozenEnvironment(frozen)) job->plc = frozen.plcName;   // nul : aucun automate relie
    else job->plcUnknown = true;
    job->folder = host_.projectPath ? host_.projectPath() : std::string{};
    job->doc = doc.get();
    job->revision = doc->revision;
    job->worker = std::thread([job] {
        XPG_TRAVAIL("ia.audit-mission");   // 1.22.2 : l'onglet Performances, travaux de fond
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

// 1.22.2 : FORCER LA FIN (voir le .hpp et mission::forceFinish).
bool Assistant::canForceFinish(std::string_view id, std::string* why) const {
    const auto fail = [&](std::string w) {
        if (why) *why = std::move(w);
        return false;
    };
    const mission::Mission* m = findMission(id);
    if (!m) return fail("Mission introuvable.");
    if (missionsReadOnly_.count(id))
        return fail("La mission " + guillemets(m->name) + " vient d'une version plus r\xC3\xA9" "cente du logiciel : elle se consulte ici, mais ne se "
                    "modifie pas.");
    if (!mission::mayForceFinish(*m, why)) return false;
    if (applyJob_)
        if (const mission::ProposalLink* l = m->proposal(applyJob_->proposalId))
            return fail("P" + std::to_string(l->order) + " de cette mission s'applique en arri\xC3\xA8re-plan : attends la fin de son application (son "
                        "compte rendu r\xC3\xA9" "el doit d'abord arriver \xC3\xA0 la mission), puis force la fin.");
    return true;
}

bool Assistant::forceFinishMission(std::string_view idv, std::string* why) {
    XPG_MESURE("ia.mission.fin-forcee");   // 1.22.2 : l'onglet Performances
    const std::string id(idv);
    if (!canForceFinish(id, why)) return false;
    // L'APPEL A L'IA EN COURS POUR ELLE EST ARRETE (comme Pause et Annuler) : marque interrompu, rien n'est propose.
    if (generating() && gen_.mission.id == id) stop();
    mission::Mission* m = missionMut(id);
    if (!m) return false;
    settleStale(*m);   // un appel reste << en cours >> d'une autre seance : interrompu
    const std::string when = isoDate(now());
    const std::vector<std::string> refused = mission::awaitingProposals(*m);
    if (!mission::forceFinish(*m, when, {}, why)) return false;
    // Leurs cartes dans les conversations : refusees aussi - une verification ou une application de fond qui les visait
    // ne les pose plus (elles ne s'appliquent que << proposees >>), et mayApply les refuse de toute facon.
    for (auto& c : conversations_) {
        bool changed = false;
        for (auto& msg : c.messages)
            if (msg.proposal && msg.proposal->state == "proposee" && listed(refused, msg.proposal->id)) {
                msg.proposal->state = "refusee";
                changed = true;
            }
        if (changed) save(c);
    }
    std::vector<std::string> tags;
    for (const auto& pid : refused)
        if (const mission::ProposalLink* l = m->proposal(pid)) tags.push_back("P" + std::to_string(l->order));
    const std::size_t n = m->forced.steps.size();
    std::string detail = n == 0 ? std::string("Aucune \xC3\xA9tape \xC3\xA0 ignorer")
                                : std::to_string(n) + (n > 1 ? " \xC3\xA9tapes ignor\xC3\xA9" "es (" : " \xC3\xA9tape ignor\xC3\xA9" "e (") + joined(m->forced.steps, ", ") + ")";
    if (!tags.empty()) detail += " ; " + joined(tags, ", ") + (tags.size() > 1 ? " non appliqu\xC3\xA9" "es" : " non appliqu\xC3\xA9" "e");
    detail += ". Rien n'est d\xC3\xA9" "fait dans le projet.";
    notifyMission(*m, "terminee", "Mission termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) : " + m->name, detail, n > 0 || !tags.empty() ? "warning" : "ok");
    saveMission(*m);
    // LE RAPPORT FINAL, comme pour une fin normale : l'audit global sur une copie du projet, sur un fil de fond ; il dit
    // << fin forcee >> et les etapes ignorees. Son projet n'est pas ouvert : le tableau de bord propose l'audit final.
    std::string w;
    const bool audit = auditMission(id, &w);
    m = missionMut(id);
    notice_ = "Assistant IA : mission " + guillemets(m ? m->name : id) + " termin\xC3\xA9" "e (fin forc\xC3\xA9" "e) - " + detail
              + (audit ? " L'audit final tourne en arri\xC3\xA8re-plan (sur une copie du projet) : le rapport suit."
                       : " Le rapport final viendra de l'audit final" + (w.empty() ? std::string(".") : " : " + w));
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

// =====================================================================================================================
//  1.22.6 : CE QUE FAIT L'IA, EN DIRECT, ET CE QUE CA COUTE (AiActivity.hpp)
// ---------------------------------------------------------------------------------------------------------------------
//  Le suivi (gen_.act) lit chaque evenement du fournisseur (pollLive) : la ligne d'activite, les etapes, les jetons de
//  chaque appel - sans revision de plus, sauf quand les etapes changent (la mise en page). A la fin de la reponse (finie,
//  en erreur, arretee), son bilan va avec le message (extra << bilan >>) ; ses etapes restent pour la ligne qui suit (la
//  verification de sa proposition, Generer et Compiler).
// =====================================================================================================================
void Assistant::startActivity(long long contextGuess) {
    activity::Tracker::Setup s;
    s.contextEstimate = contextGuess;
    s.textTool = gen_.textTool;
    const MissionCall& call = gen_.mission;
    const bool missionFix = !call.fixOf.empty();
    if (missionFix || !gen_.fixOf.empty()) {   // une correction automatique (l'avis du logiciel, 1.22.5)
        const std::string n = "Correction " + std::to_string(missionFix ? call.fixN : gen_.fixN) + "/" + std::to_string(kMaxFixes);
        const std::string who = missionFix ? call.fixWho : gen_.fixWho;
        s.purpose = n + " (" + who + ")";
        s.context = n + " (" + fixLabel(who, missionFix ? call.fixErrors : gen_.fixErrors) + ")";
    }
    if (!call.id.empty()) {   // une mission : l'etape servie
        const mission::Mission* m = findMission(call.id);
        const auto stepName = [&](const std::string& id) {
            const mission::Step* st = m ? m->step(id) : nullptr;
            return st && !st->title.empty() ? id + " : " + st->title : id;
        };
        std::string what;
        if (call.purpose == "plan") {
            what = "plan";
            if (s.purpose.empty()) s.purpose = "Plan de mission";
        } else if (call.purpose == "actualisation") {
            what = "actualisation du plan";
            if (s.purpose.empty()) s.purpose = "Actualisation du plan de mission";
        } else if (call.purpose == "correction") {
            what = "correction de " + stepName(call.step);
            if (s.purpose.empty()) s.purpose = "Correction de " + call.step;
        } else if (!call.steps.empty()) {
            what = stepName(call.steps.front()) + (call.steps.size() > 1 ? " (+" + std::to_string(call.steps.size() - 1) + ")" : std::string{});
            if (s.purpose.empty()) s.purpose = "Proposition (" + joined(call.steps, ", ") + ")";
        }
        s.context = (m && !m->name.empty() ? "Mission " + guillemets(m->name) : std::string("Mission")) + (what.empty() ? std::string{} : " \xC2\xB7 " + what)
                    + (s.context.empty() ? std::string{} : " \xC2\xB7 " + s.context);
    }
    if (gen_.split) {   // 1.21.3 : la reprise en parties d'une proposition coupee
        if (s.purpose.empty()) s.purpose = "Proposition (reprise en parties)";
        s.context += (s.context.empty() ? std::string{} : std::string(" \xC2\xB7 ")) + "Reprise en plusieurs parties";
    }
    gen_.act.start(std::move(s), lastNow_);
}

void Assistant::keepBill(Message& m, bool proposed) {
    if (!gen_.live || !gen_.act.started()) return;
    gen_.act.finish(lastNow_, proposed);
    const activity::Bill b = gen_.act.bill(lastNow_);
    if (!b.empty() && b.total() > 0) m.extra.set("bilan", activity::toJson(b));
    lastAct_ = gen_.act;
    lastActConv_ = gen_.conversation;
    lastActMsg_ = gen_.message;
}

activity::Snapshot Assistant::activity(double now) const {
    XPG_MESURE("ia.activite");   // l'onglet Performances
    activity::Snapshot s;
    const Conversation* c = current();
    if (!c) return s;
    if (generating()) {
        if (!gen_.live || !gen_.act.started() || c->id != gen_.conversation) return s;
        s = gen_.act.snapshot(now);
        s.message = gen_.message;
        return s;
    }
    // Apres la reponse : le travail de fond sur la proposition la plus recente qui en a (la verification, Generer et Compiler,
    // l'application sur une copie, une correction qui attend).
    for (int i = static_cast<int>(c->messages.size()) - 1; i >= 0; --i) {
        const auto& msg = c->messages[static_cast<std::size_t>(i)];
        if (!msg.proposal) continue;
        const Proposal& p = *msg.proposal;
        std::string line;
        double since = -1;
        if (applyJob_ && applyJob_->conversation == c->id && applyJob_->proposalId == p.id) {
            line = "Application de la proposition sur une copie du projet\xE2\x80\xA6 "
                   + std::to_string(static_cast<int>(std::lround(applyProgress() * 100.f))) + "\xC2\xA0%";
        } else if (const auto ck = std::find_if(checkJobs_.begin(), checkJobs_.end(), [&](const auto& j) { return j && j->proposalId == p.id; });
                   ck != checkJobs_.end()) {
            line = "R\xC3\xA9ponse re\xC3\xA7ue : v\xC3\xA9rification de " + plural(p.ops.size(), "op\xC3\xA9ration", "op\xC3\xA9rations") + "\xE2\x80\xA6";
            since = (*ck)->started;
        } else if (const auto bj = std::find_if(buildJobs_.begin(), buildJobs_.end(), [&](const auto& j) { return j && j->proposalId == p.id; });
                   bj != buildJobs_.end()) {
            line = "G\xC3\xA9n\xC3\xA9rer et Compiler sur une copie du projet\xE2\x80\xA6";
            since = (*bj)->started;
        } else {
            for (const auto& f : fixQueue_)
                if (f.proposalId == p.id)
                    line = "Correction " + std::to_string(f.n) + "/" + std::to_string(kMaxFixes) + " en attente (" + fixWhat(f.who, f.errors)
                           + ") : elle part \xC3\xA0 la fin de la r\xC3\xA9ponse en cours\xE2\x80\xA6";
        }
        if (line.empty()) continue;
        if (since >= 0 && now - since >= 1.0) line += " (" + activity::clockText(now - since) + ")";
        s.line = std::move(line);
        s.message = i;
        s.keepDetail = false;
        if (lastActConv_ == c->id && lastActMsg_ == i) s.history = lastAct_.history();
        return s;
    }
    return s;
}

activity::Bill Assistant::liveBill(double now) const {
    if (!generating() || !gen_.live || !gen_.act.started()) return {};
    return gen_.act.bill(now);
}

std::optional<activity::Bill> Assistant::billOf(int message) const {
    const Conversation* c = current();
    if (!c || message < 0 || message >= static_cast<int>(c->messages.size())) return std::nullopt;
    return activity::billFrom(c->messages[static_cast<std::size_t>(message)].extra);
}

} // namespace app::ai
