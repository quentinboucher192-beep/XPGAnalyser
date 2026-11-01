// =============================================================================
//  tests/ai_activite_test.cpp - 1.22.6 : ce que fait l'IA en direct, et ce que ca coute
// -----------------------------------------------------------------------------
//  Sans reseau ni horloge reelle (le temps est celui qu'on passe a tick) :
//    1. les textes (durees, nombres), la lecture de l'entree d'un outil PENDANT qu'elle s'ecrit (des morceaux coupes
//       n'importe ou : l'operation n et son nom, le plan d'une mission, un \u, le texte apres le bloc) ;
//    2. le suivi (activity::Tracker) evenement par evenement : l'envoi, la reflexion dont le compteur MONTE, l'ecriture,
//       l'operation en cours d'ecriture, un outil de l'agent (lignes_classeur, analyser_projet), l'attente du service et son
//       compte a rebours, l'estimation remplacee par le reel ; le bilan par appel (plusieurs appels dans un tour, une
//       lecture a part), son JSON relu, un message d'avant (sans bilan) ;
//    3. le flux Anthropic (thinking_delta, input_json_delta, message_delta << fin >>) et l'entree des outils en direct
//       (eager_input_streaming), puis le fournisseur sur un faux transport : l'outil repondu sur ce PC et la suite (le rang
//       de chaque appel), un service qui refuse le champ (redemande sans lui) ;
//    4. l'Assistant et le panneau, sur un faux fournisseur qui rend ses evenements PAR MORCEAUX (un morceau par image) :
//       la ligne a chaque etape, les compteurs du bas du panneau mis a jour PENDANT la reponse (env., puis le reel), le
//       bilan garde avec le message et son << + >> qui deplie le detail (dessine hors ecran), la verification d'une grosse
//       proposition, Generer et Compiler, une mission (l'etape servie), Arreter (ce qui est consomme reste compte), un
//       message 1.22.5 relu (aucun bilan, ses jetons comptent toujours).
// =============================================================================
#include "../src/app/ai/AiActivity.hpp"
#include "../src/app/ai/AiAnthropic.hpp"
#include "../src/app/ai/AiAssistant.hpp"
#include "../src/app/ai/AiHttp.hpp"
#include "../src/app/ai/AiJson.hpp"
#include "../src/app/ai/AiPanel.hpp"
#include "../src/app/ai/AiPaths.hpp"
#include "../src/app/ai/AiStore.hpp"
#include "../src/app/ai/AiUsage.hpp"
#include "../src/hmi/HmiCommands.hpp"
#include "../src/hmi/HmiEdit.hpp"
#include "../src/hmi/HmiModel.hpp"
#include "../src/menu/MenuManager.hpp"
#include "../src/ui/Theme.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
namespace ai = app::ai;
namespace act = app::ai::activity;
using ai::json::Value;

namespace {

int g_ok = 0, g_fail = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++g_ok;
    } else {
        ++g_fail;
        std::printf("  ECHEC : %s\n", what.c_str());
    }
}

bool has(std::string_view hay, std::string_view needle) { return hay.find(needle) != std::string_view::npos; }

std::string U(const fs::path& p) {
    const auto u = p.u8string();
    return std::string(u.begin(), u.end());
}

void pause(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

const std::string kNb = "\xC2\xA0";              // l'espace insecable des nombres
const std::string kEll = "\xE2\x80\xA6";
const std::string kDot = " \xC2\xB7 ";

Value O(std::initializer_list<std::pair<const char*, Value>> kv) {
    Value o = Value::object();
    for (const auto& [k, v] : kv) o.set(k, v);
    return o;
}

// Une proposition de `n` operations (des voyants) ; la 12e : Voyant_P3 sur `view`.
std::string opsInput(int n, const std::string& view) {
    Value list = Value::array();
    for (int k = 1; k <= n; ++k) {
        const std::string name = k == 12 ? std::string("Voyant_P3") : "Voyant_" + std::to_string(k);
        list.push(O({{"action", "creer"},
                     {"genre", "Indicator"},
                     {"nom", name},
                     {"vue", view},
                     {"proprietes", O({{"x", std::to_string(20 + 30 * (k % 10))}, {"y", std::to_string(20 + 30 * (k / 10))}, {"label", "P \"" + std::to_string(k) + "\""}})}}));
    }
    return ai::json::write(O({{"resume", "Les voyants des pompes"}, {"operations", list}}), false);
}

ai::ProviderEvent ev(ai::ProviderEvent::Type t, std::string text = {}, std::string detail = {}) {
    ai::ProviderEvent e;
    e.type = t;
    e.text = std::move(text);
    e.detail = std::move(detail);
    return e;
}
ai::ProviderEvent status(const std::string& s, const std::string& detail = {}) { return ev(ai::ProviderEvent::Type::Status, s, detail); }
ai::ProviderEvent text(const std::string& s) { return ev(ai::ProviderEvent::Type::Text, s); }
ai::ProviderEvent usage(long long in, long long cacheRead, long long out, bool fin = false, int call = 0) {
    ai::ProviderEvent e = ev(ai::ProviderEvent::Type::Usage, fin ? "fin" : "");
    e.usage.input = in;
    e.usage.cacheRead = cacheRead;
    e.usage.output = out;
    e.call = call;
    return e;
}
ai::ProviderEvent progress(const std::string& what, long long amount = 0, const std::string& tool = {}, const std::string& input = {}, int call = 0) {
    ai::ProviderEvent e = ev(ai::ProviderEvent::Type::Progress, what);
    e.amount = amount;
    e.toolName = tool;
    e.toolUseId = tool.empty() ? std::string{} : "toolu_" + tool;
    e.toolInputJson = input;
    e.call = call;
    return e;
}
ai::ProviderEvent toolUse(const std::string& id, const std::string& name, const std::string& input) {
    ai::ProviderEvent e = ev(ai::ProviderEvent::Type::ToolUse);
    e.toolUseId = id;
    e.toolName = name;
    e.toolInputJson = input;
    return e;
}
ai::ProviderEvent done(const std::string& reason) {
    ai::ProviderEvent e = ev(ai::ProviderEvent::Type::Done);
    e.stopReason = reason;
    return e;
}

// =============================================================================
//  1. Les textes, l'entree d'un outil lue pendant qu'elle s'ecrit
// =============================================================================
void testTexts() {
    std::printf("[textes]\n");
    check(act::clockText(12.7) == "0:12" && act::clockText(65) == "1:05" && act::clockText(3725) == "1:02:05", "les durees : 0:12, 1:05, 1:02:05");
    check(act::shortDuration(1.24) == "1,2" + kNb + "s" && act::shortDuration(12.6) == "12" + kNb + "s" && act::shortDuration(65) == "1:05",
          "les durees courtes : 1,2 s, 12 s, 1:05 (" + act::shortDuration(1.24) + ")");
    check(act::grouped(1240) == "1" + kNb + "240" && act::grouped(64000) == "64" + kNb + "000" && act::grouped(12) == "12", "les milliers : 1 240");
    check(act::estimateTokens(0) == 0 && act::estimateTokens(3) == 1 && act::estimateTokens(4960) == 1240, "env. : un jeton pour quatre octets");
    ai::ProviderRequest r;
    r.systemStable = std::string(4000, 'a');
    r.systemDynamic = std::string(400, 'b');
    ai::ProviderTurn t;
    t.text = std::string(800, 'c');
    r.turns.push_back(t);
    check(act::requestTokens(r) == 1300, "env. : les jetons de contexte d'une demande (consignes, tours)");
}

void testInputReader() {
    std::printf("[l'entree d'un outil, en morceaux]\n");
    const std::string in = opsInput(14, "Vue_Pompes");
    const std::size_t cut = in.find("\"vue\":\"Vue_Pompes\"", in.find("Voyant_P3")) + std::string("\"vue\":\"Vue_Pompes\"").size();
    check(cut != std::string::npos && cut < in.size(), "l'operation 12 dans l'entree");
    act::InputReader r;
    for (std::size_t i = 0; i < cut; i += 7) r.feed(std::string_view(in).substr(i, std::min<std::size_t>(7, cut - i)));   // coupe n'importe ou
    check(r.items() == 12 && !r.closed(), "pendant l'ecriture : l'operation 12 commencee, le tableau ouvert (" + std::to_string(r.items()) + ")");
    check(act::operationText(r.last()) == "cr\xC3\xA9" "er Voyant_P3 sur Vue_Pompes", "la derniere, en clair : " + act::operationText(r.last()));
    r.feed(std::string_view(in).substr(cut));
    check(r.items() == 14 && r.closed() && r.bytes() == in.size(), "la fin : 14 operations, le tableau ferme");
    {
        act::InputReader x;
        x.feed("{\"resume\":\"x\",\"operations\":[{\"action\":\"aligner\",\"objets\":[\"Btn_1\",\"Btn_2\"],\"bord\":\"haut\"},{\"nom\":\"Cuve_\\u00e9t\\u00e9\",\"action\":\"creer_vue\"");
        check(x.items() == 2 && x.last().count("nom") && x.last().at("nom") == "Cuve_\xC3\xA9t\xC3\xA9", "un \\u dans un nom : relu en UTF-8");
        check(act::operationText(x.last()) == "cr\xC3\xA9" "er la vue Cuve_\xC3\xA9t\xC3\xA9", "creer_vue : " + act::operationText(x.last()));
        act::InputReader y;
        y.feed("{\"operations\":[{\"action\":\"aligner\",\"objets\":[\"Btn_1\",\"Btn_2\"],\"vue\":\"Vue_A\"}]}\n```\nEt voici {\"operations\":[{},{}]}");
        check(y.items() == 1 && y.closed() && act::operationText(y.last()) == "aligner Btn_1 sur Vue_A", "objets : le premier ; le texte apres le bloc ne compte pas");
        act::InputReader z("etapes");
        z.feed("{\"nom\":\"Station\",\"etapes\":[{\"id\":\"S1\",\"titre\":\"Cr\xC3\xA9" "er\"},{\"titre\":\"Configurations de gaz\",\"id\":\"S3b\"");
        check(z.items() == 2 && z.last().at("id") == "S3b" && z.last().at("titre") == "Configurations de gaz", "le plan d'une mission : ses etapes");
    }
}

// =============================================================================
//  2. Le suivi, evenement par evenement (une horloge d'essai)
// =============================================================================
void testTracker() {
    std::printf("[le suivi d'une reponse]\n");
    act::Tracker tr;
    act::Tracker::Setup su;
    su.contextEstimate = 48000;
    tr.start(su, 0.0);
    check(tr.snapshot(0.2).line == "Envoi de la demande (env. 48" + kNb + "k jetons de contexte)" + kEll, "l'envoi : " + tr.snapshot(0.2).line);
    check(tr.snapshot(3.4).line == "Envoi de la demande (env. 48" + kNb + "k jetons de contexte)" + kEll + " (0:03)", "l'envoi qui dure : " + tr.snapshot(3.4).line);
    // message_start : la reflexion commence, l'entree est dite
    check(tr.observe(status("reflexion"), 1.2), "une etape de plus : la mise en page suit");
    (void)tr.observe(usage(7000, 41000, 1), 1.2);
    {
        const auto s = tr.snapshot(1.2);
        check(!s.history.empty() && s.history[0].text == "Demande envoy\xC3\xA9" "e : 48" + kNb + "k jetons de contexte (dont 41" + kNb + "k en cache)"
                  && s.history[0].seconds > 1.19 && s.history[0].seconds < 1.21,
              "l'etape passee : la demande envoyee, son contexte exact (" + (s.history.empty() ? std::string{} : s.history[0].text) + ")");
        check(tr.contextNow() == 48000 && tr.liveTokens() == 48001 && tr.estimating(), "les jetons d'entree des message_start ; la sortie pas encore dite (env.)");
    }
    // la reflexion : le compteur monte
    (void)tr.observe(progress("reflexion", 0), 1.3);
    (void)tr.observe(progress("reflexion", 992), 4.0);
    const std::string early = tr.snapshot(4.0).line;
    const long long tokensEarly = tr.liveTokens();
    for (int k = 0; k < 4; ++k) (void)tr.observe(progress("reflexion", 992), 5.0 + k * 2.0);
    const std::string late = tr.snapshot(13.2).line;
    check(early == "L'IA r\xC3\xA9" "fl\xC3\xA9" "chit" + kEll + " env. 248 jetons de r\xC3\xA9" "flexion (0:02)", "la reflexion : " + early);
    check(late == "L'IA r\xC3\xA9" "fl\xC3\xA9" "chit" + kEll + " env. 1" + kNb + "240 jetons de r\xC3\xA9" "flexion (0:12)", "le compteur monte : " + late);
    check(tr.liveTokens() > tokensEarly && tr.liveTokens() == 48000 + 1240, "les jetons en cours montent avec la reflexion (env.)");
    // l'ecriture
    (void)tr.observe(status("ecriture"), 14.0);
    (void)tr.observe(text("Je propose les voyants "), 14.1);
    (void)tr.observe(text("des pompes, sur la vue."), 14.4);
    check(tr.snapshot(14.5).line == "L'IA \xC3\xA9" "crit sa r\xC3\xA9ponse" + kEll + " 9 mots", "l'ecriture : " + tr.snapshot(14.5).line);
    check(tr.snapshot(14.5).history[0].text == "L'IA a r\xC3\xA9" "fl\xC3\xA9" "chi : env. 1" + kNb + "240 jetons", "la reflexion passee : " + tr.snapshot(14.5).history[0].text);
    // l'outil qui s'ecrit : l'operation n, sans attendre la fin
    const std::string in = opsInput(14, "Vue_Pompes");
    const std::size_t cut = in.find("\"vue\":\"Vue_Pompes\"", in.find("Voyant_P3")) + 18;
    (void)tr.observe(status("operations"), 15.0);
    (void)tr.observe(progress("entree", 0, "proposer_operations", ""), 15.0);
    check(tr.snapshot(15.1).line == "L'IA \xC3\xA9" "crit la proposition" + kEll, "la proposition commence : " + tr.snapshot(15.1).line);
    for (std::size_t i = 0; i < cut; i += 40) (void)tr.observe(progress("entree", 0, "proposer_operations", in.substr(i, std::min<std::size_t>(40, cut - i))), 16.0);
    check(tr.snapshot(16.0).line == "L'IA \xC3\xA9" "crit la proposition : op\xC3\xA9ration 12 (cr\xC3\xA9" "er Voyant_P3 sur Vue_Pompes)" + kEll,
          "l'operation en cours : " + tr.snapshot(16.0).line);
    (void)tr.observe(progress("entree", 0, "proposer_operations", in.substr(cut)), 18.0);
    check(tr.snapshot(18.0).line == "L'IA termine la proposition : 14 op\xC3\xA9rations" + kEll, "le tableau ferme : " + tr.snapshot(18.0).line);
    // le reel remplace l'estimation (message_delta)
    const long long before = tr.liveTokens();
    (void)tr.observe(usage(7000, 41000, 5200, true), 19.0);
    check(before > 48001 && !tr.estimating() && tr.liveTokens() == 48000 + 5200, "message_delta : le reel remplace l'estimation (plus de env.)");
    (void)tr.observe(toolUse("toolu_1", "proposer_operations", in), 19.0);
    tr.finish(19.5, true);
    check(tr.snapshot(20).line.empty() && tr.snapshot(20).history.size() == act::kHistory, "finie : plus de ligne, les dernieres etapes restent");
    check(tr.snapshot(20).history[0].text == "L'IA a \xC3\xA9" "crit la proposition : 14 op\xC3\xA9rations", "l'etape passee : " + tr.snapshot(20).history[0].text);
    const act::Bill b = tr.bill(30);
    check(b.calls.size() == 1 && b.calls[0].purpose == "Proposition" && b.calls[0].outputKnown && b.calls[0].output() == 5200 && !b.estimated(),
          "le bilan : un appel, la proposition, la sortie dite");
    check(b.calls[0].thinking > 1240 && b.calls[0].thinking < 5200 && !b.calls[0].thinkingExact,
          "la reflexion : la sortie dite moins ce qui est arrive en clair (env. " + std::to_string(b.calls[0].thinking) + ")");
    check(act::billLine(b) == "53,2" + kNb + "k jetons" + kDot + "0:19", "la ligne du bilan : " + act::billLine(b));
    const auto detail = act::billDetail(b);
    check(detail.size() == 2 && has(detail[0], "1. Proposition") && has(detail[0], "entr\xC3\xA9" "e 48" + kNb + "k (dont 41" + kNb + "k lus du cache)")
              && has(detail[0], "sortie 5,2" + kNb + "k") && has(detail[0], "env. ") && has(detail[0], "de r\xC3\xA9" "flexion") && has(detail[1], "Total : 53,2"),
          "le detail : " + (detail.empty() ? std::string{} : detail[0]));
}

void testTrackerTools() {
    std::printf("[plusieurs appels dans un tour : les outils de l'agent, une lecture a part, l'attente]\n");
    act::Tracker tr;
    act::Tracker::Setup su;
    su.context = "Mission \xC2\xAB Station \xC2\xBB \xC2\xB7 S3b : Configurations de gaz";
    tr.start(su, 0.0);
    check(tr.snapshot(0).context == su.context, "le contexte : l'etape servie");
    (void)tr.observe(status("lecture", "Lecture de \xC2\xAB plan.pdf \xC2\xBB : partie 1/3" + kEll), 0.5);
    check(tr.snapshot(0.6).line == "Lecture de \xC2\xAB plan.pdf \xC2\xBB : partie 1/3" + kEll, "une lecture a part : " + tr.snapshot(0.6).line);
    ai::ProviderEvent pre = usage(900, 0, 120);
    pre.preread = true;
    (void)tr.observe(pre, 2.0);
    (void)tr.observe(status("connexion"), 2.0);
    (void)tr.observe(status("reflexion"), 3.0);
    (void)tr.observe(usage(2000, 10000, 1, false, 0), 3.0);
    (void)tr.observe(status("operations"), 3.5);
    (void)tr.observe(progress("entree", 0, "lignes_classeur", "{\"fichier\":\"Liste"), 3.5);
    check(tr.snapshot(3.6).line == "L'IA pr\xC3\xA9pare l'appel de l'outil lignes_classeur" + kEll, "un outil qui s'ecrit : " + tr.snapshot(3.6).line);
    (void)tr.observe(usage(2000, 10000, 80, true, 0), 4.0);
    (void)tr.observe(progress("outil", 0, "lignes_classeur", "{\"fichier\":\"Liste_ES.xlsx\",\"a_partir_de\":200,\"combien\":200}"), 4.0);
    check(tr.snapshot(4.2).line == "L'IA lit le classeur Liste_ES.xlsx (outil lignes_classeur, lignes 200 \xC3\xA0 399)" + kEll,
          "l'outil de l'agent : " + tr.snapshot(4.2).line);
    (void)tr.observe(status("outil", "Lecture des lignes d'un classeur joint"), 4.1);
    check(has(tr.snapshot(4.2).line, "Liste_ES.xlsx"), "ce que l'outil dit de lui-meme ne remplace pas une phrase plus precise");
    (void)tr.observe(progress("appel", 0, "lignes_classeur", {}, 1), 4.5);
    check(tr.snapshot(4.6).line == "Envoi de la suite \xC3\xA0 l'IA (le r\xC3\xA9sultat de lignes_classeur)" + kEll, "la suite : " + tr.snapshot(4.6).line);
    // le service demande d'attendre (429)
    ai::ProviderEvent wait = progress("attente", 0, {}, {}, 1);
    wait.seconds = 8;
    wait.httpStatus = 429;
    (void)tr.observe(wait, 5.0);
    (void)tr.observe(status("reprise", "Trop de demandes (429). Nouvelle tentative (1/2) dans 8 s" + kEll), 5.0);
    check(tr.snapshot(5.0).line == "Attente du service (limite : nouvelle tentative dans 8" + kNb + "s)" + kEll, "l'attente : " + tr.snapshot(5.0).line);
    check(tr.snapshot(8.0).line == "Attente du service (limite : nouvelle tentative dans 5" + kNb + "s)" + kEll && tr.snapshot(8.0).keepDetail,
          "le compte a rebours (la cause reste dite) : " + tr.snapshot(8.0).line);
    (void)tr.observe(status("connexion"), 13.0);
    (void)tr.observe(status("reflexion"), 14.0);
    (void)tr.observe(usage(3000, 10000, 1, false, 1), 14.0);
    (void)tr.observe(progress("outil", 0, "analyser_projet", "{\"question\":\"orphelins\"}", 1), 15.0);
    check(tr.snapshot(15.2).line == "Analyse du projet (analyser_projet : orphelins)" + kEll, "analyser_projet : " + tr.snapshot(15.2).line);
    (void)tr.observe(progress("appel", 0, "analyser_projet", {}, 2), 15.5);
    (void)tr.observe(usage(4000, 10000, 3000, true, 2), 20.0);
    (void)tr.observe(text("Fait."), 20.0);
    const ai::ProviderUsage real = tr.realUsage();
    check(real.input == 9000 && real.cacheRead == 30000 && real.output == 3081, "le reel : la SOMME des appels (avant : le dernier remplacait les autres)");
    tr.finish(21.0, false);
    const act::Bill b = tr.bill(21.0);
    check(b.calls.size() == 4, "le bilan : une lecture a part et trois appels (" + std::to_string(b.calls.size()) + ")");
    if (b.calls.size() == 4) {
        check(b.calls[0].aside && b.calls[0].purpose == "Lecture \xC3\xA0 part : Lecture de \xC2\xAB plan.pdf \xC2\xBB : partie 1/3" && b.calls[0].total() == 1020,
              "1 : la lecture a part (" + b.calls[0].purpose + ")");
        check(b.calls[1].purpose == "Lecture du classeur Liste_ES.xlsx (lignes_classeur)" && b.calls[1].total() == 12080, "2 : " + b.calls[1].purpose);
        check(b.calls[2].purpose == "Analyse du projet (analyser_projet : orphelins)" && b.calls[2].total() == 13001, "3 : " + b.calls[2].purpose);
        check(b.calls[3].purpose == "R\xC3\xA9ponse" && b.calls[3].total() == 17000 && b.calls[3].seconds > 5.4, "4 : la reponse");
        check(b.total() == 1020 + 12080 + 13001 + 17000 && act::billLine(b) == "43,1" + kNb + "k jetons" + kDot + "0:21" + kDot + "4 appels",
              "le total : " + act::billLine(b));
    }
    // le JSON garde avec le message, relu ; un message d'avant (sans bilan) : rien
    Value extra = Value::object();
    extra.set("jetons", static_cast<std::int64_t>(b.total()));
    extra.set("bilan", act::toJson(b));
    const auto back = act::billFrom(extra);
    check(back && back->calls.size() == 4 && back->total() == b.total() && back->calls[1].purpose == b.calls[1].purpose && back->calls[0].aside
              && act::billLine(*back) == act::billLine(b) && act::billDetail(*back) == act::billDetail(b),
          "le bilan relu : le meme");
    Value old = Value::object();
    old.set("jetons", static_cast<std::int64_t>(12300));
    check(!act::billFrom(old) && !act::billFrom(Value{}), "un message 1.22.5 (ses jetons seulement) : pas de bilan");
    // une reprise en parties : la reponse coupee montree avec (deja comptee)
    act::Tracker again;
    again.start({}, 30.0);
    again.carry(b);
    (void)again.observe(usage(1000, 0, 10, true), 31);
    again.finish(32, true);
    const act::Bill b2 = again.bill(32);
    check(b2.calls.size() == 5 && b2.calls[0].carried && has(b2.calls[0].purpose, "R\xC3\xA9ponse coup\xC3\xA9" "e") && has(act::billDetail(b2)[0], "(d\xC3\xA9j\xC3\xA0 compt\xC3\xA9)")
              && again.liveTokens() == 1010,
          "une reprise en parties : les appels de la reponse coupee, dits deja comptes (hors du direct)");
}

// =============================================================================
//  3. Le flux Anthropic, l'entree des outils en direct, le fournisseur sur un faux transport
// =============================================================================
std::string sse(const std::string& event, const Value& data) { return "event: " + event + "\ndata: " + ai::json::write(data, false) + "\n\n"; }
Value usageJson(long long in, long long cacheRead, long long out) {
    return O({{"input_tokens", static_cast<std::int64_t>(in)}, {"cache_read_input_tokens", static_cast<std::int64_t>(cacheRead)},
              {"cache_creation_input_tokens", 0}, {"output_tokens", static_cast<std::int64_t>(out)}});
}
std::string msgStart(long long in = 7000, long long cacheRead = 41000) {
    return sse("message_start", O({{"type", "message_start"},
                                   {"message", O({{"id", "msg_1"}, {"type", "message"}, {"role", "assistant"}, {"content", Value::array()}, {"usage", usageJson(in, cacheRead, 1)}})}}));
}
std::string blockStart(int i, const Value& block) { return sse("content_block_start", O({{"type", "content_block_start"}, {"index", i}, {"content_block", block}})); }
std::string delta(int i, const Value& d) { return sse("content_block_delta", O({{"type", "content_block_delta"}, {"index", i}, {"delta", d}})); }
std::string blockStop(int i) { return sse("content_block_stop", O({{"type", "content_block_stop"}, {"index", i}})); }
std::string msgEnd(const std::string& reason, long long out) {
    return sse("message_delta", O({{"type", "message_delta"}, {"delta", O({{"stop_reason", reason}})}, {"usage", O({{"output_tokens", static_cast<std::int64_t>(out)}})}}))
           + sse("message_stop", O({{"type", "message_stop"}}));
}
std::string toolStream(const std::string& id, const std::string& name, const std::string& input, long long out, bool thinking) {
    std::string s = msgStart();
    int i = 0;
    if (thinking) {
        s += blockStart(i, O({{"type", "thinking"}, {"thinking", ""}}));
        s += delta(i, O({{"type", "thinking_delta"}, {"thinking", std::string(600, 'r')}})) + delta(i, O({{"type", "thinking_delta"}, {"thinking", std::string(400, 'r')}}));
        s += blockStop(i++);
    }
    s += blockStart(i, O({{"type", "tool_use"}, {"id", id}, {"name", name}, {"input", Value::object()}}));
    for (std::size_t k = 0; k < input.size(); k += 50) s += delta(i, O({{"type", "input_json_delta"}, {"partial_json", input.substr(k, 50)}}));
    s += blockStop(i);
    return s + msgEnd("tool_use", out);
}
std::string textStream(const std::string& t, long long out) {
    return msgStart(3000, 50000) + blockStart(0, O({{"type", "text"}, {"text", ""}})) + delta(0, O({{"type", "text_delta"}, {"text", t}})) + blockStop(0)
           + msgEnd("end_turn", out);
}

void testStream() {
    std::printf("[le flux Anthropic]\n");
    const std::string in = opsInput(14, "Vue_Station");
    const std::string stream = toolStream("toolu_1", "proposer_operations", in, 5200, true);
    ai::AnthropicStream s;
    std::vector<ai::ProviderEvent> all;
    for (std::size_t i = 0; i < stream.size(); i += 13) {   // coupe n'importe ou
        s.feed(stream.substr(i, 13));
        for (auto& e : s.take()) all.push_back(std::move(e));
    }
    s.end();
    for (auto& e : s.take()) all.push_back(std::move(e));
    long long thinking = 0;
    std::string chunks, toolName;
    int blocks = 0;
    const ai::ProviderEvent* fin = nullptr;
    for (const auto& e : all) {
        if (e.type == ai::ProviderEvent::Type::Progress && e.text == "reflexion") {
            thinking += e.amount;
            if (e.amount == 0) ++blocks;
        }
        if (e.type == ai::ProviderEvent::Type::Progress && e.text == "entree") {
            chunks += e.toolInputJson;
            toolName = e.toolName;
        }
        if (e.type == ai::ProviderEvent::Type::Usage && e.text == "fin") fin = &e;
    }
    check(blocks == 1 && thinking == 1000, "la reflexion : un bloc, sa longueur au fil de l'eau (jamais son texte)");
    check(chunks == in && toolName == "proposer_operations", "l'entree de l'outil en morceaux (input_json_delta), avant la fin du message");
    check(fin && fin->usage.output == 5200 && fin->usage.input == 7000 && fin->usage.cacheRead == 41000, "message_delta : l'usage de la fin, marque << fin >>");
    // eager_input_streaming : les outils de la demande, pas ceux du service
    const std::string tools = "[{\"name\":\"proposer_operations\",\"input_schema\":{\"type\":\"object\"}},{\"type\":\"web_search_20260209\",\"name\":\"web_search\"}]";
    Value v;
    check(ai::json::parse(ai::eagerTools(tools), v) && v.isArray() && v.items().size() == 2 && v.items()[0].flag("eager_input_streaming")
              && !v.items()[1].has("eager_input_streaming") && ai::eagerTools("pas du JSON") == "pas du JSON",
          "eager_input_streaming : sur les outils de la demande seulement");
}

// ---- le faux transport : il rejoue des reponses, retient les requetes --------------------------------------------------
struct Scripted {
    int                      status{200};
    std::vector<std::string> chunks;
};
struct FakeNet {
    std::mutex                   m;
    std::deque<Scripted>         replies;
    std::vector<ai::HttpRequest> requests;
};
FakeNet g_net;

class FakeHttp final : public ai::HttpClient {
public:
    ai::HttpResponse perform(const ai::HttpRequest& req, const ai::HttpSink& sink) override {
        Scripted s;
        {
            std::lock_guard<std::mutex> lk(g_net.m);
            g_net.requests.push_back(req);
            if (!g_net.replies.empty()) {
                s = std::move(g_net.replies.front());
                g_net.replies.pop_front();
            } else {
                s.status = 500;
                s.chunks = {"{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"rien de prevu\"}}"};
            }
        }
        ai::HttpResponse r;
        if (cancelled_) {
            r.failure = ai::HttpResponse::Failure::Cancelled;
            return r;
        }
        r.status = s.status;
        r.headers = {{"content-type", s.status == 200 ? "text/event-stream" : "application/json"}};
        if (sink.onHeaders) sink.onHeaders(s.status, r.headers);
        for (const auto& c : s.chunks)
            if (sink.onData) sink.onData(c);
        return r;
    }
    void cancel() override { cancelled_ = true; }
    [[nodiscard]] bool cancelled() const noexcept override { return cancelled_; }

private:
    std::atomic<bool> cancelled_{false};
};

std::vector<ai::ProviderEvent> runToEnd(ai::Provider& p, int ms = 5000) {
    std::vector<ai::ProviderEvent> all;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < until) {
        for (auto& e : p.poll()) all.push_back(std::move(e));
        if (!all.empty() && (all.back().type == ai::ProviderEvent::Type::Done || all.back().type == ai::ProviderEvent::Type::Error) && !p.busy()) break;
        pause(2);
    }
    for (auto& e : p.poll()) all.push_back(std::move(e));
    return all;
}

void testProvider() {
    std::printf("[le fournisseur : l'outil repondu sur ce PC, la suite, le champ refuse]\n");
    {
        std::lock_guard<std::mutex> lk(g_net.m);
        g_net.replies.clear();
        g_net.requests.clear();
    }
    ai::setHttpFactory([] { return std::make_unique<FakeHttp>(); });
    // 1 : un relais qui refuse eager_input_streaming ; 2 : l'IA appelle lignes_classeur ; 3 : la suite, du texte.
    g_net.replies.push_back({400, {"{\"type\":\"error\",\"error\":{\"type\":\"invalid_request_error\",\"message\":\"tools.0.eager_input_streaming: Extra inputs are not permitted\"}}"}});
    g_net.replies.push_back({200, {toolStream("toolu_r", "lignes_classeur", "{\"fichier\":\"Liste_ES.xlsx\",\"a_partir_de\":200,\"combien\":200}", 80, false)}});
    g_net.replies.push_back({200, {textStream("Le classeur a 400 lignes.", 30)}});
    ai::AnthropicProvider p;
    ai::ProviderRequest r;
    r.model = "modele-essai";
    r.apiKey = "cle";
    r.system = "Les consignes.";
    r.toolsJson = "[{\"name\":\"proposer_operations\",\"description\":\"x\",\"input_schema\":{\"type\":\"object\"}}]";
    ai::ProviderTurn t;
    t.text = "Lis le classeur";
    r.turns.push_back(t);
    ai::LocalTool rows;
    rows.name = "lignes_classeur";
    rows.definitionJson = "{\"name\":\"lignes_classeur\",\"description\":\"x\",\"input_schema\":{\"type\":\"object\"}}";
    rows.run = [](const std::string&, const std::function<void(const std::string&)>& say, const std::atomic<bool>&) {
        say("Lecture des lignes d'un classeur joint");
        return std::string("200 lignes");
    };
    r.localTools.push_back(rows);
    std::string why;
    check(p.start(r, &why), "le fournisseur part (" + why + ")");
    const auto all = runToEnd(p);
    std::size_t sent = 0;
    std::vector<ai::HttpRequest> reqs;
    {
        std::lock_guard<std::mutex> lk(g_net.m);
        reqs = g_net.requests;
        sent = reqs.size();
    }
    check(sent == 3, "trois requetes : le champ refuse, l'appel de l'outil, la suite (" + std::to_string(sent) + ")");
    if (sent == 3) {
        check(has(reqs[0].body, "eager_input_streaming") && !has(reqs[1].body, "eager_input_streaming") && !has(reqs[2].body, "eager_input_streaming"),
              "l'entree des outils en direct demandee, puis retiree quand le service la refuse");
    }
    const ai::ProviderEvent *outil = nullptr, *appel = nullptr;
    int usage0 = 0, usage1 = 0;
    for (const auto& e : all) {
        if (e.type == ai::ProviderEvent::Type::Progress && e.text == "outil") outil = &e;
        if (e.type == ai::ProviderEvent::Type::Progress && e.text == "appel") appel = &e;
        if (e.type == ai::ProviderEvent::Type::Usage && !e.usage.empty()) (e.call == 0 ? usage0 : usage1) += 1;
    }
    check(outil && outil->toolName == "lignes_classeur" && has(outil->toolInputJson, "Liste_ES.xlsx") && outil->call == 0, "l'outil repondu ici : son nom, son entree");
    check(appel && appel->call == 1 && appel->toolName == "lignes_classeur", "la suite : un nouvel appel (rang 1)");
    check(usage0 > 0 && usage1 > 0, "les jetons de chaque appel, avec leur rang");
    check(!all.empty() && all.front().type == ai::ProviderEvent::Type::Status && all.front().text == "connexion" && all.back().type == ai::ProviderEvent::Type::Done,
          "le premier etat : connexion ; la fin : Done");
    // Le suivi de ces evenements : deux appels, leurs buts.
    act::Tracker tr;
    tr.start({}, 0);
    double now = 0;
    for (const auto& e : all) (void)tr.observe(e, now += 0.1);
    tr.finish(now, false);
    const act::Bill b = tr.bill(now);
    check(b.calls.size() == 2 && b.calls[0].purpose == "Lecture du classeur Liste_ES.xlsx (lignes_classeur)" && b.calls[0].total() == 48080
              && b.calls[1].purpose == "R\xC3\xA9ponse" && b.calls[1].total() == 53030,
          "le bilan : la lecture du classeur, puis la reponse (" + act::billLine(b) + ")");
    ai::setHttpFactory({});
}

// =============================================================================
//  4. L'Assistant et le panneau : un faux fournisseur qui rend ses evenements PAR MORCEAUX
// =============================================================================
struct StepScript {
    std::vector<ai::ProviderRequest>                       requests;
    std::deque<std::deque<std::vector<ai::ProviderEvent>>> replies;   // par demande : un morceau par image
    int                                                    cancels{0};
};
class StepProvider final : public ai::Provider {
public:
    explicit StepProvider(StepScript& s) : s_(s) {}
    [[nodiscard]] std::string_view key() const noexcept override { return "anthropic"; }
    bool start(ai::ProviderRequest r, std::string*) override {
        s_.requests.push_back(std::move(r));
        if (!s_.replies.empty()) {
            batches_ = std::move(s_.replies.front());
            s_.replies.pop_front();
        }
        busy_ = true;
        return true;
    }
    std::vector<ai::ProviderEvent> poll() override {
        if (batches_.empty()) return {};
        auto b = std::move(batches_.front());
        batches_.pop_front();
        if (batches_.empty()) busy_ = false;
        return b;
    }
    void cancel() override {
        ++s_.cancels;
        batches_.clear();
        busy_ = false;
    }
    [[nodiscard]] bool busy() const noexcept override { return busy_; }

private:
    StepScript&                                 s_;
    std::deque<std::vector<ai::ProviderEvent>> batches_;
    bool                                        busy_{false};
};

struct Recorder final : gfx::IRenderer {
    std::vector<std::string> texts;
    void beginFrame(gfx::Color) override {}
    void endFrame() override {}
    void pushClip(const gfx::Rect&) override {}
    void popClip() override {}
    void fillRect(const gfx::Rect&, gfx::Color) override {}
    void strokeRect(const gfx::Rect&, gfx::Color, float) override {}
    void fillRoundedRect(const gfx::Rect&, gfx::Color, float) override {}
    void line(gfx::Point, gfx::Point, gfx::Color, float) override {}
    void drawText(gfx::Point, std::string_view t, gfx::FontId, gfx::Color) override { texts.emplace_back(t); }
    void drawTexture(const gfx::Rect&, gfx::TextureId, gfx::Color) override {}
    void fillTriangles(const gfx::Vertex*, std::size_t) override {}
    gfx::TextMetrics measure(std::string_view t, gfx::FontId f) const override {
        return {static_cast<float>(t.size()) * f.v * 0.5f, static_cast<float>(f.v), 0.8f * f.v, 0.2f * f.v};
    }
    float lineHeight(gfx::FontId f) const override { return static_cast<float>(f.v); }
    gfx::Size surfaceSize() const override { return {1600.f, 900.f}; }
    float dpiScale() const override { return 1.f; }
    std::size_t fitCharacters(std::string_view t, gfx::FontId f, float w) const override {
        return std::min(t.size(), static_cast<std::size_t>(std::max(0.f, w / (f.v * 0.5f))));
    }
    [[nodiscard]] bool saw(std::string_view what) const {
        return std::any_of(texts.begin(), texts.end(), [&](const std::string& t) { return t.find(what) != std::string::npos; });
    }
};

struct Fixture {
    fs::path                     dir;
    StepScript                   s;   // avant le panneau : il lui survit
    hmi::DocumentPtr             doc = std::make_shared<hmi::Document>();
    core::CommandStack           stack;
    hmi::Id                      view{hmi::kNoId};
    menu::MenuManager            mm{menu::MenuFactory{}};
    std::unique_ptr<ai::AiPanel> panel;
    double                       t{100.0};

    explicit Fixture(const std::string& name, bool fresh = true) {
        dir = fs::temp_directory_path() / ("xpg_ai_activite_" + name);
        if (fresh) fs::remove_all(dir);
        fs::create_directories(dir / "Station_Gaz");
        ai::setRoot(U(dir / "IA"));
        auto& p = doc->project;
        hmi::View v = hmi::makeView(p, "Vue_Station");
        p.views = {v};
        view = p.views.front().id;
        hmi::Variable var;
        var.id = p.allocate();
        var.name = "Pompe_1_Marche";
        var.type = "BOOL";
        p.programs.variables.push_back(var);
        p.config.created = "2026-10-01T08:00:00";
        ai::Assistant::Host core;
        core.document = [this] { return doc; };
        core.activeView = [this] { return view; };
        core.selection = [] { return std::vector<hmi::Id>{}; };
        core.apply = [this](core::CommandPtr c) { (void)stack.push(std::move(c)); };
        core.projectName = [] { return std::string("Station_Gaz"); };
        core.projectPath = [this] { return U(dir / "Station_Gaz"); };
        ai::AiPanel::UiHost ui;
        ui.showDialog = [this](menu::MenuPtr d, std::function<void(const menu::DialogResult&)> cb) { mm.ShowDialog(std::move(d), std::move(cb)); };
        panel = std::make_unique<ai::AiPanel>("ai.panneau", std::move(core), std::move(ui));
        panel->setBounds({0, 0, 460, 900});
        panel->ensureLoaded();
        panel->assistant().setProviderFactory([this](std::string_view) { return std::make_unique<StepProvider>(s); });
        panel->layout();
    }
    ~Fixture() {
        // les travaux de fond (verification, Generer et Compiler) finissent avant le panneau
        for (int k = 0; k < 600 && panel && (as().buildingAny() || as().applyingAny()); ++k) {
            step(0.05);
            pause(5);
        }
    }
    [[nodiscard]] ai::Assistant& as() { return panel->assistant(); }
    [[nodiscard]] ai::AiChatView& chat() { return panel->chat(); }
    void configure(bool build = false) {
        ai::Settings st = as().settings();
        st.provider = "anthropic";
        st.model = "modele-essai";
        st.buildCheck = build;
        st.autoCorrect = false;
        st.localFirst = false;
        as().setSettings(st);
        as().setApiKey("cle-essai");
        as().newConversation();
        panel->sync();
        panel->layout();
        step();   // l'horloge de l'Assistant (le premier tick, comme a chaque image)
    }
    void step(double dt = 0.3) {
        t += dt;
        panel->tick(t);
        mm.applyPending();
        panel->layout();
    }
    [[nodiscard]] std::string line() {
        const auto l = chat().activityLines();
        return l.empty() ? std::string{} : (l.size() > 1 && !has(l[0], "L'IA") && !has(l[0], "Envoi") && has(l[0], "Mission") ? l[1] : l[0]);
    }
    [[nodiscard]] std::string band() {
        std::string out;
        for (const auto& l : panel->usageBand().lines) out += l + " | ";
        return out;
    }
    [[nodiscard]] int lastIndex() { return static_cast<int>(as().current()->messages.size()) - 1; }
    void render(Recorder& rec) {
        panel->layout();
        panel->render(ui::PaintContext{rec, ui::Theme::dark(), panel->bounds(), t, nullptr});
    }
};

void testLive() {
    std::printf("[l'Assistant et le panneau : la ligne, les compteurs pendant la reponse, le bilan et son +]\n");
    Fixture f("direct");
    f.configure();
    const std::string in = opsInput(14, "Vue_Station");
    const std::size_t cut = in.find("\"vue\":\"Vue_Station\"", in.find("Voyant_P3")) + 19;
    ai::ProviderEvent limits = ev(ai::ProviderEvent::Type::Usage);
    limits.limits.requestsLimit = 4000;
    limits.limits.requestsRemaining = 3999;
    limits.limits.tokensLimit = 2000000;
    limits.limits.tokensRemaining = 1900000;
    f.s.replies.push_back({{status("connexion")},
                           {limits, status("reflexion"), usage(7000, 41000, 1)},
                           {progress("reflexion", 0), progress("reflexion", 2000)},
                           {progress("reflexion", 2960)},
                           {status("ecriture"), text("Je propose les voyants des pompes.")},
                           {status("operations"), progress("entree", 0, "proposer_operations", ""), progress("entree", 0, "proposer_operations", in.substr(0, 200))},
                           {progress("entree", 0, "proposer_operations", in.substr(200, cut - 200))},
                           {progress("entree", 0, "proposer_operations", in.substr(cut)), usage(7000, 41000, 5200, true)},
                           {toolUse("toolu_1", "proposer_operations", in), done("tool_use")}});
    const double sentAt = f.t;
    check(f.panel->sendText("Ajoute les voyants des pompes"), "envoyer");
    f.step();   // connexion
    check(has(f.line(), "Envoi de la demande (env. ") && has(f.line(), "jetons de contexte)"), "1 : l'envoi, son contexte estime : " + f.line());
    f.step();   // message_start
    check(has(f.line(), "L'IA r\xC3\xA9" "fl\xC3\xA9" "chit"), "2 : la reflexion : " + f.line());
    {
        const auto lines = f.chat().activityLines();
        check(lines.size() >= 2 && has(lines[1], "Demande envoy\xC3\xA9" "e : 48" + kNb + "k jetons de contexte (dont 41" + kNb + "k en cache)"),
              "2 : l'etape passee dit le contexte exact (message_start)");
        check(has(f.band(), "3999/4000 requ\xC3\xAA" "tes") && has(f.band(), "Cette conversation : env. 48" + kNb + "k jetons")
                  && has(f.band(), "aujourd'hui env. 48" + kNb + "k"),
              "2 : PENDANT la reponse - la limite, la conversation et la journee (env.) : " + f.band());
    }
    f.step();
    f.step();   // la reflexion a monte
    check(has(f.line(), "env. 1" + kNb + "240 jetons de r\xC3\xA9" "flexion"), "4 : le compteur de la reflexion : " + f.line());
    check(has(f.band(), "Cette conversation : env. 49,2" + kNb + "k"), "4 : la conversation monte pendant la reflexion : " + f.band());
    f.step();
    check(f.line() == "L'IA \xC3\xA9" "crit sa r\xC3\xA9ponse" + kEll + " 6 mots", "5 : l'ecriture : " + f.line());
    f.step();
    f.step();
    check(f.line() == "L'IA \xC3\xA9" "crit la proposition : op\xC3\xA9ration 12 (cr\xC3\xA9" "er Voyant_P3 sur Vue_Station)" + kEll, "7 : l'operation en cours : " + f.line());
    check(f.as().generating() && f.as().phase() == ai::Assistant::Phase::Proposing, "7 : toujours en cours (l'operation dite AVANT la fin)");
    {
        Recorder first;
        f.render(first);   // la ligne demande une seconde rangee : la mise en page suit (l'image suivante)
        Recorder rec;
        f.render(rec);
        check(rec.saw("op\xC3\xA9ration 12") && rec.saw("Voyant_P3 sur Vue_Station)" + kEll), "7 : dessine sous les points : la ligne, sur deux rangees");
        check(rec.saw("Pr\xC3\xA9paration des op\xC3\xA9rations") && rec.saw("Cette r\xC3\xA9ponse : env. "), "7 : l'indicateur, et le bilan en cours (env.)");
        check(f.chat().activityToggleRect().w > 0.f, "7 : l'icone de l'historique est dessinee");
    }
    f.step();   // message_delta : le reel
    check(f.as().generating() && !has(f.band(), "env.") && has(f.band(), "Cette conversation : 53,2" + kNb + "k jetons"),
          "8 : le reel remplace l'estimation, encore pendant la reponse : " + f.band());
    f.step();   // la fin
    check(!f.as().generating() && f.line().empty(), "9 : finie : plus de ligne (aucun travail de fond)");
    const int m = f.lastIndex();
    const auto bill = f.as().billOf(m);
    check(bill && bill->calls.size() == 1 && bill->calls[0].purpose == "Proposition" && bill->total() == 53200 && !bill->estimated(),
          "le bilan garde avec le message : la proposition, 53,2 k jetons");
    check(f.as().current()->messages[static_cast<std::size_t>(m)].extra.integer("jetons") == 53200, "les jetons du message (1.16.0) : inchanges");
    check(bill && bill->seconds > 2.6 && bill->seconds < 2.8 && f.t - sentAt > 2.6, "sa duree : celle de l'horloge de l'Assistant");
    // Le << + >> : dessine hors ecran, un clic deplie le detail par action.
    Recorder r1;
    f.render(r1);
    const gfx::Rect plus = f.chat().billRect(m);
    check(r1.saw("53,2" + kNb + "k jetons" + kDot + "0:02") && r1.saw("+") && plus.w > 0.f, "sous la reponse : << 53,2 k jetons . 0:02 >> et son +");
    check(!r1.saw("Total : "), "replie : pas de detail");
    (void)f.chat().dispatch(ui::MouseDown{{plus.x + plus.w / 2.f, plus.y + plus.h / 2.f}, ui::MouseButton::Left, 1, {}});
    check(f.chat().billOpen(m), "un clic sur + : deplie");
    Recorder r2;
    f.render(r2);
    check(r2.saw("1. Proposition") && r2.saw("lus du cache") && r2.saw("Total : 53,2" + kNb + "k jetons") && r2.saw("\xE2\x80\x93"),
          "deplie : une ligne par appel, le total ; le bouton dit -");
    check(f.chat().billDetailLines(m).size() == 2, "deux lignes de detail");
    (void)f.chat().dispatch(ui::MouseDown{{plus.x + plus.w / 2.f, plus.y + plus.h / 2.f}, ui::MouseButton::Left, 1, {}});
    check(!f.chat().billOpen(m), "un second clic : replie");
    // Relu d'un fichier : le bilan revient avec le message.
    f.as().load();
    f.panel->sync();
    int again = -1;
    for (std::size_t i = 0; i < f.as().current()->messages.size(); ++i)
        if (f.as().current()->messages[i].proposal) again = static_cast<int>(i);
    const auto reread = f.as().billOf(again);
    check(reread && reread->total() == 53200 && reread->calls[0].purpose == "Proposition", "relu du disque : le meme bilan");
}

void testBackground() {
    std::printf("[apres la reponse : la verification d'une grosse proposition, Generer et Compiler]\n");
    {
        Fixture f("verification");
        f.configure();
        const std::string in = opsInput(45, "Vue_Station");
        f.s.replies.push_back({{status("reflexion"), usage(9000, 0, 1)},
                               {status("operations"), progress("entree", 0, "proposer_operations", in)},
                               {usage(9000, 0, 4000, true), toolUse("toolu_v", "proposer_operations", in), done("tool_use")}});
        check(f.panel->sendText("Ajoute 45 voyants"), "envoyer");
        f.step();
        f.step();
        f.step();
        check(!f.as().generating(), "la reponse est finie");
        const auto lines = f.chat().activityLines();
        check(!lines.empty() && lines[0] == "R\xC3\xA9ponse re\xC3\xA7ue : v\xC3\xA9rification de 45 op\xC3\xA9rations" + kEll,
              "la verification de fond : " + (lines.empty() ? std::string{} : lines[0]));
        check(lines.size() >= 2 && lines[1].rfind("\xE2\x9C\x93 L'IA a \xC3\xA9" "crit la proposition : 45 op\xC3\xA9rations", 0) == 0,
              "les etapes de la reponse restent dessous : " + (lines.size() > 1 ? lines[1] : std::string{}));
        f.chat().setActivityHistoryOpen(false);
        check(f.chat().activityLines().size() == 1, "l'historique replie");
        f.chat().setActivityHistoryOpen(true);
        for (int k = 0; k < 400 && !f.chat().activityLines().empty(); ++k) {
            f.step(0.05);
            pause(5);
        }
        check(f.chat().activityLines().empty(), "la verification finie : plus de ligne");
    }
    {
        Fixture f("logiciel");
        f.configure(true);
        const std::string in = opsInput(2, "Vue_Station");
        f.s.replies.push_back({{status("reflexion"), usage(9000, 0, 1)}, {usage(9000, 0, 300, true), toolUse("toolu_b", "proposer_operations", in), done("tool_use")}});
        check(f.panel->sendText("Ajoute deux voyants"), "envoyer");
        f.step();
        f.step();
        check(!f.as().generating() && f.as().buildingAny(), "Generer et Compiler tournent sur un fil de fond");
        check(f.line().rfind("G\xC3\xA9n\xC3\xA9rer et Compiler sur une copie du projet" + kEll, 0) == 0, "la ligne : " + f.line());
        for (int k = 0; k < 600 && f.as().buildingAny(); ++k) {
            f.step(0.05);
            pause(5);
        }
        f.step();
        check(!f.as().buildingAny() && f.line().empty(), "l'avis arrive : plus de ligne");
    }
}

std::string planInput() {
    Value steps = Value::array();
    steps.push(O({{"id", "S1"}, {"titre", "Configurations de gaz"}, {"detail", "Les configurations."}, {"categorie", "vues"}, {"depend_de", Value::array()},
                  {"operations", 1}, {"obligatoire", true}, {"produit", Value::array()}}));
    return ai::json::write(O({{"nom", "Station de gaz"}, {"description", "Une mission d'essai."}, {"etapes", steps}}), false);
}

void testMission() {
    std::printf("[une mission : l'etape servie]\n");
    Fixture f("mission");
    f.configure();
    f.s.replies.push_back({{status("reflexion"), usage(9000, 0, 1)}, {text("J'ai analys\xC3\xA9 le projet.")},
                           {usage(9000, 0, 400, true), toolUse("toolu_plan", "planifier_mission", planInput()), done("tool_use")}});
    std::string why;
    check(f.as().createMission("Construis la station de gaz compl\xC3\xA8te : les variables, les alarmes, les scripts et les vues.", &why),
          "creer la mission (" + why + ")");
    f.step();
    {
        const auto lines = f.chat().activityLines();
        check(lines.size() >= 2 && lines[0].rfind("Mission \xC2\xAB ", 0) == 0 && has(lines[0], "\xC2\xB7 plan"), "le plan : le contexte (" + (lines.empty() ? std::string{} : lines[0]) + ")");
    }
    f.step();
    f.step();
    check(!f.as().generating(), "le plan est arrive");
    const int planMsg = [&] {
        const auto& msgs = f.as().current()->messages;
        for (int i = static_cast<int>(msgs.size()) - 1; i >= 0; --i)
            if (msgs[static_cast<std::size_t>(i)].tool && msgs[static_cast<std::size_t>(i)].tool->name == "planifier_mission") return i;
        return -1;
    }();
    const auto planBill = f.as().billOf(planMsg);
    check(planBill && planBill->calls.size() == 1 && planBill->calls[0].purpose == "Plan de mission", "le bilan du plan : << Plan de mission >>");
    const std::string id = f.as().missions().empty() ? std::string{} : f.as().missions().front().id;
    f.s.replies.push_back({{status("reflexion"), usage(9000, 0, 1)}, {status("operations"), progress("entree", 0, "proposer_operations", opsInput(1, "Vue_Station"))},
                           {usage(9000, 0, 300, true), toolUse("toolu_p1", "proposer_operations", opsInput(1, "Vue_Station")), done("tool_use")}});
    check(f.as().continueMission(id, &why), "la suite de la mission (" + why + ")");
    f.step();
    {
        const auto lines = f.chat().activityLines();
        check(!lines.empty() && lines[0] == "Mission \xC2\xAB Station de gaz \xC2\xBB \xC2\xB7 S1 : Configurations de gaz",
              "la proposition : l'etape servie (" + (lines.empty() ? std::string{} : lines[0]) + ")");
    }
    f.step();
    f.step();
    const auto pb = f.as().billOf(f.lastIndex()) ? f.as().billOf(f.lastIndex()) : f.as().billOf(f.lastIndex() - 1);
    check(pb && pb->calls.size() == 1 && pb->calls[0].purpose == "Proposition (S1)", "le bilan de la proposition : << Proposition (S1) >>");
}

void testStopAndOld() {
    std::printf("[Arreter : ce qui est consomme reste compte ; un message 1.22.5 relu]\n");
    Fixture f("arret");
    f.configure();
    const long long before = f.as().usageTotals().tokensToday;
    f.s.replies.push_back({{status("reflexion"), usage(20000, 0, 1)}, {status("ecriture"), text("Je commence")}, {text(" a ecrire ...")}});
    check(f.panel->sendText("Ecris beaucoup"), "envoyer");
    f.step();
    f.step();
    check(has(f.band(), "aujourd'hui env. 20" + kNb + "k"), "pendant : aujourd'hui compte la reponse en cours : " + f.band());
    f.as().stop();
    f.step();
    const auto& m = f.as().current()->messages.back();
    check(m.state == "arrete" && m.extra.integer("jetons") == 20001 && f.as().usageTotals().tokensToday == before + 20001,
          "Arreter : les jetons deja consommes vont au message et au cumul du jour");
    const auto b = f.as().billOf(f.lastIndex());
    check(b && b->calls.size() == 1 && b->total() >= 20001, "Arreter : son bilan est garde");
    // Un message ecrit par la 1.22.5 (ses jetons, aucun bilan) : relu tel quel.
    ai::Conversation c;
    c.id = "c-20261101-ancienne";
    c.title = "Ancienne";
    c.created = c.modified = "2026-10-31T10:00:00";
    ai::Message u;
    u.id = "m1";
    u.role = "utilisateur";
    u.text = "Une cuve";
    u.date = c.created;
    ai::Message a;
    a.id = "m2";
    a.role = "assistant";
    a.text = "Voici une cuve.";
    a.state = "fini";
    a.date = c.created;
    a.provider = "anthropic";
    a.model = "modele-essai";
    a.extra = Value::object();
    a.extra.set("jetons", static_cast<std::int64_t>(12300));
    c.messages = {u, a};
    std::string why;
    check(ai::saveConversation(c, &why), "le fichier 1.22.5 ecrit (" + why + ")");
    f.as().load();
    check(f.as().select(c.id), "la conversation d'avant, ouverte");
    f.panel->sync();
    check(!f.as().billOf(1), "un message 1.22.5 : pas de bilan (absent = rien)");
    Recorder rec;
    f.render(rec);
    check(rec.saw("cuve.") && f.chat().billRect(1).w == 0.f, "dessine : sa reponse, sans + ");
    f.step();
    check(has(f.band(), "Cette conversation : 12,3" + kNb + "k jetons"), "ses jetons comptent toujours : " + f.band());
}

} // namespace

int main() {
    std::printf("== ai_activite_test : ce que fait l'IA en direct, et ce que ca coute (1.22.6) ==\n");
    testTexts();
    testInputReader();
    testTracker();
    testTrackerTools();
    testStream();
    testProvider();
    testLive();
    testBackground();
    testMission();
    testStopAndOld();
    std::printf("\n%d verifications reussies, %d en echec\n", g_ok, g_fail);
    return g_fail == 0 ? 0 : 1;
}
