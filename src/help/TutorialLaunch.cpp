#include "help/TutorialLaunch.hpp"

#include <algorithm>
#include <deque>
#include <map>
#include <fstream>
#include <memory>
#include <sstream>

namespace help {
namespace {

struct Registry {
    std::deque<std::unique_ptr<Tutorial>> all;   // des adresses stables
    TutorialLauncher launcher;
    // Section 5 (tranche 9) : les sujets, le deducteur (T3) et le cache des deduits.
    std::vector<TopicInfo> topics;
    TutorialDeducer deducer;
    struct Deduced { std::unique_ptr<Tutorial> tutorial; std::string fault; };
    std::map<std::string, Deduced, std::less<>> deduced;   // par cle de sujet
    std::deque<std::unique_ptr<Tutorial>> retired;          // vides du cache : les adresses restent valables
};

Registry& registry() {
    static Registry r;
    return r;
}

} // namespace

std::string registerTutorialText(std::string_view text, std::vector<TutorialProblem>* problems) {
    std::vector<TutorialProblem> local;
    auto t = std::make_unique<Tutorial>(Tutorial::parse(text, &local));
    const bool ok = local.empty() && !t->id.empty();
    if (problems) problems->insert(problems->end(), local.begin(), local.end());
    if (!ok) return {};
    auto& all = registry().all;
    const std::string id = t->id;
    const auto it = std::find_if(all.begin(), all.end(), [&](const auto& p) { return p->id == id; });
    if (it != all.end()) *it = std::move(t);
    else all.push_back(std::move(t));
    return id;
}

int registerTutorialDir(const std::filesystem::path& dir, std::vector<std::string>* errors) {
    std::error_code ec;
    std::vector<std::filesystem::path> files;
    for (const auto& e : std::filesystem::directory_iterator(dir, ec))
        if (e.path().extension() == ".tuto") files.push_back(e.path());
    if (ec && errors) errors->push_back(dir.string() + " : " + ec.message());
    std::sort(files.begin(), files.end());
    int n = 0;
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::stringstream ss;
        ss << in.rdbuf();
        std::vector<TutorialProblem> problems;
        if (!registerTutorialText(ss.str(), &problems).empty()) { ++n; continue; }
        if (errors)
            for (const auto& p : problems)
                errors->push_back(f.filename().string() + ":" + std::to_string(p.line) + " : " + p.message);
    }
    return n;
}

namespace {
void retireDeduced() {
    auto& r = registry();
    for (auto& [key, d] : r.deduced)
        if (d.tutorial) r.retired.push_back(std::move(d.tutorial));
    r.deduced.clear();
}
} // namespace

void clearTutorials() {
    auto& r = registry();
    r.all.clear();
    r.deduced.clear();
    r.retired.clear();
}

int registerEmbeddedTutorials(std::vector<std::string>* errors) {
    int n = 0;
    for (const auto& e : embeddedTutorials()) {
        std::vector<TutorialProblem> problems;
        if (!registerTutorialText(e.text, &problems).empty()) { ++n; continue; }
        if (errors)
            for (const auto& p : problems)
                errors->push_back(std::string(e.file) + ":" + std::to_string(p.line) + " : " + p.message);
    }
    return n;
}

std::vector<const Tutorial*> tutorials() {
    std::vector<const Tutorial*> out;
    for (const auto& t : registry().all) out.push_back(t.get());
    return out;
}

const Tutorial* findTutorial(std::string_view key) {
    const auto& all = registry().all;
    for (const auto& t : all)
        if (t->id == key) return t.get();
    for (const auto& t : all)
        if (std::find(t->topics.begin(), t->topics.end(), key) != t->topics.end()) return t.get();
    // Tranche 9 : un deduit deja fait se retrouve par son id (le lecteur, une session).
    for (const auto& [topic, d] : registry().deduced)
        if (d.tutorial && d.tutorial->id == key) return d.tutorial.get();
    return nullptr;
}

void setTutorialLauncher(TutorialLauncher launcher) { registry().launcher = std::move(launcher); }

std::optional<std::size_t> nearestATry(const CompiledTutorial& c, std::size_t step) {
    if (c.steps.empty()) return std::nullopt;
    step = std::min(step, c.steps.size() - 1);
    for (std::size_t k = step; k < c.steps.size(); ++k)
        if (c.steps[k].aTry) return k;
    for (std::size_t k = step; k-- > 0;)
        if (c.steps[k].aTry) return k;
    return std::nullopt;
}

std::string aTryNotice(std::optional<std::size_t> aTryStep, std::size_t asked) {
    if (!aTryStep)
        return "Ce tutoriel n'a pas d'\xC3\xA9tape \xC2\xAB \xC3\x80 toi \xC2\xBB : il te montre les gestes. "
               "Refais-les ensuite dans ton projet.";
    if (*aTryStep == asked) return {};
    return "\xC2\xAB Essayer \xC2\xBB commence \xC3\xA0 l'\xC3\xA9tape " + std::to_string(*aTryStep + 1)
           + (*aTryStep > asked ? std::string(", la premi\xC3\xA8re o\xC3\xB9 c'est \xC3\xA0 toi : celles d'avant sont faites pour toi.")
                                : std::string(", la plus proche o\xC3\xB9 c'est \xC3\xA0 toi."));
}

StartResult startTutorial(std::string_view key, std::string_view variant, TutorialStart options) {
    const Tutorial* t = findTutorial(key);
    if (!t) t = tutorialForTopic(key);   // tranche 9 : le deduit du sujet
    if (!t) return StartResult::Unknown;
    if (!variant.empty()) options.variant = std::string(variant);
    const auto c = t->compile(options.variant);
    if (!c.problems.empty() || c.steps.empty()) return StartResult::Broken;
    options.variant = c.variant;
    options.step = std::min(options.step, c.steps.size() - 1);
    // 1.11.2 (R1112-4) : "Essayer" sur une etape sans A toi (vues : l'etape 1 n'en a pas) rejouait la
    // demonstration sans un mot. On ouvre sur l'A toi le plus proche (seek remet le bac dans l'etat
    // exact du debut de cette etape), et le lecteur dit ou il ouvre ; sans aucun A toi, il le dit.
    if (options.aTry && !c.steps[options.step].aTry) {
        const auto k = nearestATry(c, options.step);
        options.notice = aTryNotice(k, options.step);
        if (k) options.step = *k;
        else options.aTry = false;
    }
    auto& launcher = registry().launcher;
    if (!launcher) return StartResult::NoLauncher;
    return launcher(*t, options) ? StartResult::Started : StartResult::Refused;
}

// ---- Section 5 : un tutoriel pour chaque sujet -------------------------------------
// topicKindName : en ligne dans TutorialLaunch.hpp (integration I111).

void setTopics(std::vector<TopicInfo> list) {
    retireDeduced();
    registry().topics = std::move(list);
}

const std::vector<TopicInfo>& topics() { return registry().topics; }

const TopicInfo* findTopic(std::string_view key) {
    for (const auto& t : registry().topics)
        if (t.key == key) return &t;
    return nullptr;
}

void setTutorialDeducer(TutorialDeducer deducer) {
    retireDeduced();
    registry().deducer = std::move(deducer);
}

bool hasTutorialDeducer() { return static_cast<bool>(registry().deducer); }

std::string deducedTutorialText(std::string_view key) {
    const auto* info = findTopic(key);
    auto& deducer = registry().deducer;
    if (!info || !deducer) return {};
    return deducer(*info);
}

const Tutorial* tutorialForTopic(std::string_view key, std::string* fault) {
    if (const Tutorial* t = findTutorial(key)) return t;   // l'ecrit passe devant
    auto& r = registry();
    if (const auto it = r.deduced.find(key); it != r.deduced.end()) {
        if (fault) *fault = it->second.fault;
        return it->second.tutorial.get();
    }
    const auto* info = findTopic(key);
    if (!info) { if (fault) *fault = "sujet inconnu"; return nullptr; }
    if (!r.deducer) { if (fault) *fault = "pas de tutoriel (ni \xC3\xA9" "crit, ni d\xC3\xA9" "duit)"; return nullptr; }
    Registry::Deduced d;
    const std::string text = r.deducer(*info);
    if (text.empty()) d.fault = "le d\xC3\xA9" "ducteur ne sait pas faire ce sujet";
    else {
        std::vector<TutorialProblem> problems;
        auto t = std::make_unique<Tutorial>(Tutorial::parse(text, &problems));
        if (!problems.empty()) d.fault = "ligne " + std::to_string(problems.front().line) + " : " + problems.front().message;
        else if (t->id.empty()) d.fault = "le texte d\xC3\xA9" "duit n'a pas d'id (= <id> | <titre>)";
        else {
            if (std::find(t->topics.begin(), t->topics.end(), info->key) == t->topics.end()) t->topics.push_back(info->key);
            d.tutorial = std::move(t);
        }
    }
    if (fault) *fault = d.fault;
    const auto ins = r.deduced.emplace(std::string(key), std::move(d)).first;
    return ins->second.tutorial.get();
}

bool isDeducedTutorial(const Tutorial* t) {
    if (!t) return false;
    for (const auto& [key, d] : registry().deduced)
        if (d.tutorial.get() == t) return true;
    return false;
}

bool tutorialExpected(const TopicInfo& t) {
    return !(t.kind == TopicKind::Special && t.key.rfind("notes-", 0) == 0);
}

TutorialCount countTutorials() {
    TutorialCount c;
    for (const auto& info : registry().topics) {
        // Decision 12 (03/10) : les notes de version n'ont pas de tutoriel ; hors du compte.
        if (!tutorialExpected(info)) { c.excluded.push_back(info.key); continue; }
        ++c.total;
        std::string fault;
        const Tutorial* t = tutorialForTopic(info.key, &fault);
        if (!t) {
            if (fault.rfind("pas de tutoriel", 0) == 0 || fault.rfind("le d\xC3\xA9" "ducteur ne sait", 0) == 0)
                c.missing.push_back(info.key);
            else c.broken.push_back(info.key + " : " + fault);
            continue;
        }
        std::vector<std::string> variants = t->variants;
        if (variants.empty()) variants.emplace_back();
        std::string first;
        for (const auto& v : variants) {
            const auto compiled = t->compile(v);
            if (!compiled.problems.empty())
                first = (v.empty() ? std::string() : v + ", ") + "ligne " + std::to_string(compiled.problems.front().line)
                      + " : " + compiled.problems.front().message;
            else if (compiled.steps.empty()) first = (v.empty() ? std::string() : v + ", ") + "aucune \xC3\xA9tape";
            if (!first.empty()) break;
        }
        if (!first.empty()) { c.broken.push_back(info.key + " : " + first); continue; }
        ++c.ready;
        if (isDeducedTutorial(t)) ++c.deduced;
        else ++c.written;
    }
    return c;
}

const char* startResultName(StartResult r) {
    switch (r) {
    case StartResult::Started: return "lanc\xC3\xA9";
    case StartResult::Unknown: return "inconnu";
    case StartResult::Broken: return "illisible";
    case StartResult::NoLauncher: return "pas de lanceur";
    case StartResult::Refused: return "refus\xC3\xA9";
    }
    return "?";
}

} // namespace help
