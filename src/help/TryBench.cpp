// =============================================================================
//  help/TryBench.cpp
// =============================================================================
#include "TryBench.hpp"

#include <algorithm>
#include <cctype>

namespace help {

const std::deque<TrendSample> TryBench::kEmptyTrend{};

namespace {

// Un releve de session -> des lignes de table. Le releve porte deja tout ce
// qu'il faut ; ce qu'on ajoute ici, c'est le souvenir de la valeur d'avant.
// Un booleen se reconnait a son TYPE. La valeur seule ne suffit pas : "0" et
// "1" sont aussi des INT, et la premiere version proposait de basculer un
// compteur qui passait par zero. Sans type (une ancienne session), on retombe
// sur TRUE / FALSE, que seul un BOOL affiche.
bool looksBoolean(const std::string& type, const std::string& v) {
    if (!type.empty()) return type == "BOOL";
    return v == "TRUE" || v == "FALSE";
}

double numberOf(const std::string& v) {
    if (v == "TRUE")  return 1.0;
    if (v == "FALSE") return 0.0;
    try { return std::stod(v); } catch (...) { return 0.0; }
}

} // namespace

TryBench::TryBench(std::shared_ptr<TrySession> session) : session_(std::move(session)) {
    // UN PREMIER CYCLE TOUT DE SUITE, ET C'EST VOULU. Une table vide a
    // l'ouverture n'apprend rien, et oblige a deviner qu'il faut appuyer sur
    // quelque chose. Le banc s'ouvre donc sur un etat, a l'arret.
    sample(true);
}

// --------------------------------------------------------------- transport --
void TryBench::play()  { if (session_) state_ = State::Running; }
void TryBench::pause() { if (state_ == State::Running) state_ = State::Paused; }

void TryBench::stop() {
    if (!session_) return;

    // LES FORCAGES NE PARTENT PAS AVEC LE RESET, et c'est le test qui l'a dit :
    // `Runtime::reset` remet les valeurs, pas la liste des forcages. Un Stop qui
    // laisse une entree collee rend le cycle suivant inexplicable - on croit
    // avoir tout remis a zero et une voie reste a TRUE sans que rien ne le dise.
    for (const auto& r : rows_)
        if (r.forced) (void)session_->unforce(r.name);
    session_->reset();
    state_     = State::Stopped;
    scans_     = 0;
    elapsedMs_ = 0;
    carryMs_   = 0;
    for (auto& t : trends_) t.clear();
    // Un arret remet la machine dans l'etat ou on l'a trouvee : les forcages
    // partent avec, sinon "Stop" laisserait une entree collee et le cycle
    // suivant serait inexplicable.
    sample(true);
}

void TryBench::step() {
    if (!session_) return;
    const auto report = session_->step(periodMs_);
    ran_     = report.ran;
    failure_ = report.failure;
    ++scans_;
    elapsedMs_ += periodMs_;
    sample(false);
    // On passe en PAUSE, d'ou qu'on vienne. Rester "a l'arret" apres avoir
    // lance un cycle ferait mentir le bandeau : la machine a tourne, elle est
    // quelque part, et c'est exactement ce que veut dire la pause.
    state_ = State::Paused;
}

std::uint32_t TryBench::tick(std::int64_t deltaMs) {
    if (state_ != State::Running || !session_ || deltaMs <= 0) return 0;

    carryMs_ += deltaMs;
    std::uint32_t ran = 0;
    // UN PLAFOND PAR IMAGE. Une fenetre reduite puis rouverte rend un delta
    // enorme ; sans plafond, le banc lancerait deux mille cycles d'un coup et
    // l'application se figerait le temps de les faire.
    constexpr std::uint32_t kMaxPerFrame = 8;
    while (carryMs_ >= periodMs_ && ran < kMaxPerFrame) {
        carryMs_ -= periodMs_;
        const auto report = session_->step(periodMs_);
        ran_     = report.ran;
        failure_ = report.failure;
        ++scans_;
        elapsedMs_ += periodMs_;
        ++ran;
        // UN CYCLE QUI ECHOUE ARRETE LE BANC. Continuer afficherait cinquante
        // fois la meme erreur et donnerait l'impression que ca tourne.
        if (!report.ran) { state_ = State::Paused; break; }
    }
    if (carryMs_ > periodMs_ * 4) carryMs_ = 0;   // on ne rattrape pas le passe
    if (ran > 0) sample(false);
    return ran;
}

void TryBench::setPeriodMs(std::int64_t ms) {
    periodMs_ = std::clamp<std::int64_t>(ms, 1, 10000);
}

// ------------------------------------------------------------- le releve ----
void TryBench::sample(bool first) {
    if (!session_) return;

    // Ce qu'on affichait avant, pour savoir ce qui a bouge.
    std::vector<std::string> previous;
    previous.reserve(rows_.size());
    for (const auto& r : rows_) previous.push_back(r.value);

    const TryReport report = session_->observe();   // une lecture, pas un cycle
    ran_ = first ? report.ran : ran_;

    std::vector<WatchRow> rows;
    rows.reserve(report.all.size());
    for (const auto& o : report.all) {
        WatchRow row;
        row.name    = o.name;
        row.value   = o.value;
        row.forced  = o.forced;
        row.type    = o.type;
        row.boolean = looksBoolean(o.type, o.value);
        row.number  = numberOf(o.value);
        rows.push_back(std::move(row));
    }

    // `changed` se calcule sur le NOM, pas sur le rang : la liste peut changer
    // de longueur entre deux releves, et comparer ligne a ligne ferait clignoter
    // tout le tableau des qu'une variable apparait.
    if (!first) {
        for (std::size_t i = 0; i < rows.size(); ++i) {
            auto& row = rows[i];
            // Le cas courant d'abord : meme liste, meme rang. Chercher par nom
            // a chaque fois coutait un million de comparaisons par cycle sur
            // les 1400 variables d'un bloc d'equipement - a cinquante cycles
            // par seconde, la table ramait avant l'automate.
            if (i < rows_.size() && rows_[i].name == row.name) {
                row.changed = rows_[i].value != row.value;
                continue;
            }
            const auto at = std::find_if(rows_.begin(), rows_.end(),
                                         [&](const WatchRow& old) { return old.name == row.name; });
            if (at != rows_.end()) row.changed = at->value != row.value;
        }
    }
    rows_ = std::move(rows);

    // La courbe : un point par cycle, pour chaque signal suivi.
    for (std::size_t i = 0; i < trendNames_.size() && i < trends_.size(); ++i) {
        const auto at = std::find_if(rows_.begin(), rows_.end(),
                                     [&](const WatchRow& r) { return r.name == trendNames_[i]; });
        if (at == rows_.end()) continue;
        trends_[i].push_back(TrendSample{scans_, at->number});
        while (trends_[i].size() > kTrendDepth) trends_[i].pop_front();
    }
}

const std::vector<std::string>& TryBench::notes() const {
    static const std::vector<std::string> none;
    return session_ ? session_->notes() : none;
}
const std::vector<std::string>& TryBench::declared() const {
    static const std::vector<std::string> none;
    return session_ ? session_->declared() : none;
}
const std::string& TryBench::example() const {
    static const std::string none;
    return session_ ? session_->example() : none;
}

// ----------------------------------------------------------------- agir -----
bool TryBench::force(const std::string& name, const std::string& value) {
    if (!session_ || !session_->force(name, value)) return false;
    // UN FORCAGE SE VOIT TOUT DE SUITE. Attendre le prochain cycle laisserait
    // croire que le clic n'a rien fait, et a l'arret il n'y a pas de prochain
    // cycle du tout.
    step();
    return true;
}

bool TryBench::unforce(const std::string& name) {
    if (!session_ || !session_->unforce(name)) return false;
    step();
    return true;
}

bool TryBench::toggle(const std::string& name) {
    const auto at = std::find_if(rows_.begin(), rows_.end(),
                                 [&](const WatchRow& r) { return r.name == name; });
    if (at == rows_.end() || !at->boolean) return false;
    return force(name, at->number != 0.0 ? "FALSE" : "TRUE");
}

// --------------------------------------------------------------- la courbe --
void TryBench::addToTrend(const std::string& name) {
    if (inTrend(name)) return;
    // Quatre signaux, pas plus : au-dela les courbes se recouvrent et la
    // lecture y perd plus qu'elle n'y gagne.
    if (trendNames_.size() >= 4) {
        trendNames_.erase(trendNames_.begin());
        trends_.erase(trends_.begin());
    }
    trendNames_.push_back(name);
    trends_.emplace_back();

    // Le point d'aujourd'hui, pour que la courbe commence quelque part plutot
    // que d'attendre le cycle suivant.
    const auto at = std::find_if(rows_.begin(), rows_.end(),
                                 [&](const WatchRow& r) { return r.name == name; });
    if (at != rows_.end()) trends_.back().push_back(TrendSample{scans_, at->number});
}

void TryBench::removeFromTrend(const std::string& name) {
    for (std::size_t i = 0; i < trendNames_.size(); ++i) {
        if (trendNames_[i] != name) continue;
        trendNames_.erase(trendNames_.begin() + static_cast<std::ptrdiff_t>(i));
        trends_.erase(trends_.begin() + static_cast<std::ptrdiff_t>(i));
        return;
    }
}

bool TryBench::inTrend(const std::string& name) const {
    return std::find(trendNames_.begin(), trendNames_.end(), name) != trendNames_.end();
}

const std::deque<TrendSample>& TryBench::trend(const std::string& name) const {
    for (std::size_t i = 0; i < trendNames_.size() && i < trends_.size(); ++i)
        if (trendNames_[i] == name) return trends_[i];
    return kEmptyTrend;
}

TryBench::Bounds TryBench::trendBounds() const {
    Bounds b{0.0, 1.0};
    bool first = true;
    for (const auto& serie : trends_)
        for (const auto& s : serie) {
            if (first) { b.low = b.high = s.value; first = false; }
            b.low  = std::min(b.low, s.value);
            b.high = std::max(b.high, s.value);
        }
    // UN SIGNAL PLAT DOIT SE TRACER QUAND MEME. Sans cette marge, low == high,
    // la division par la hauteur fait un zero, et la courbe disparait - ce qui
    // ressemble a un bug alors que c'est une constante.
    if (b.high - b.low < 1e-9) { b.low -= 0.5; b.high += 0.5; }
    return b;
}

// ------------------------------------------------------------ l'appel ----
namespace {

std::string squeeze(std::string_view t) {
    std::string out;
    bool space = false;
    for (const char c : t) {
        const bool blank = c == ' ' || c == '\t' || c == '\n' || c == '\r';
        if (blank) { space = !out.empty(); continue; }
        if (space) out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out;
}

bool identChar(char c) {
    const auto u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '_';
}

bool sameName(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::toupper(static_cast<unsigned char>(a[i]))
            != std::toupper(static_cast<unsigned char>(b[i]))) return false;
    return true;
}

} // namespace

std::string instanceOf(const std::vector<std::string>& declared, std::string_view type) {
    for (const auto& d : declared) {
        const auto colon = d.find(" : ");
        if (colon == std::string::npos) continue;
        const std::string name = squeeze(std::string_view(d).substr(0, colon));
        std::string t = squeeze(std::string_view(d).substr(colon + 3));
        // "INT   (adresse directe)" : ce qui suit le type n'en fait pas partie.
        if (const auto sp = t.find(' '); sp != std::string::npos) t.resize(sp);
        if (sameName(t, type) && !name.empty()) return name;
    }
    return {};
}

std::vector<CallArgument> callArguments(std::string_view example, std::string_view instance) {
    std::vector<CallArgument> out;
    if (instance.empty()) return out;

    // Les commentaires deviennent des blancs : un "(* une heure *)" au milieu
    // de la liste d'arguments n'est pas un argument, et ses parentheses
    // fausseraient le comptage.
    std::string text(example);
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] != '(' || text[i + 1] != '*') continue;
        const auto end = text.find("*)", i + 2);
        const auto stop = end == std::string::npos ? text.size() : end + 2;
        for (std::size_t k = i; k < stop; ++k) if (text[k] != '\n') text[k] = ' ';
        i = stop - 1;
    }

    // Le premier appel : le nom entier (pas la fin d'un autre), puis "(".
    std::size_t open = std::string::npos;
    for (std::size_t at = text.find(instance); at != std::string::npos;
         at = text.find(instance, at + 1)) {
        if (at > 0 && identChar(text[at - 1])) continue;
        // La fin du nom n'a pas besoin d'etre verifiee a part : "ROT_GavageBis("
        // a un 'B' la ou il faut une parenthese, et c'est le test qui suit.
        std::size_t k = at + instance.size();
        while (k < text.size() && std::isspace(static_cast<unsigned char>(text[k]))) ++k;
        if (k < text.size() && text[k] == '(') { open = k; break; }
    }
    if (open == std::string::npos) return out;

    int depth = 0;
    std::size_t start = open + 1;
    for (std::size_t i = open; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '(' || c == '[') { ++depth; continue; }
        const bool closing = c == ')' || c == ']';
        if (closing) --depth;
        const bool cut = (c == ',' && depth == 1) || (c == ')' && depth == 0);
        if (!cut) continue;
        const std::string_view piece(text.data() + start, i - start);
        start = i + 1;
        const auto assign = piece.find(":=");
        const auto arrow  = piece.find("=>");
        const auto sep = std::min(assign, arrow);
        if (sep != std::string_view::npos) {
            CallArgument a;
            a.param    = squeeze(piece.substr(0, sep));
            a.argument = squeeze(piece.substr(sep + 2));
            a.output   = sep == arrow;
            if (!a.param.empty()) out.push_back(std::move(a));
        }
        if (depth == 0) break;
    }
    return out;
}

} // namespace help
