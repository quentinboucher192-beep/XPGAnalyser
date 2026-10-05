#include "help/TutorialPlayer.hpp"

#include <algorithm>

namespace help {

TutorialPlayer::TutorialPlayer(const Tutorial& tutorial, TutorialStage& stage, std::string_view variant)
    : tutorial_(tutorial), stage_(stage), compiled_(tutorial.compile(variant)) {}

double TutorialPlayer::timeMs() const {
    if (compiled_.steps.empty()) return 0;
    return compiled_.steps[step_].startMs + stepTime_;
}

bool TutorialPlayer::canATry() const {
    return !compiled_.steps.empty() && compiled_.steps[step_].aTry.has_value();
}

std::string TutorialPlayer::progressLabel() const {
    const auto n = compiled_.steps.size();
    return std::to_string(n ? step_ + 1 : 0) + " / " + std::to_string(n);
}

std::string TutorialPlayer::clockLabel() const {
    return formatClock(static_cast<int>(timeMs())) + " / " + formatClock(compiled_.totalMs);
}

void TutorialPlayer::start(std::size_t step, bool paused) {
    state_ = PlayerState::Paused;
    seek(step, 0);
    if (!compiled_.steps.empty() && !paused) state_ = PlayerState::Playing;
}

// Joue, dans l'etape en cours, ce qui tombe entre les instants from et to.
void TutorialPlayer::playRange(double from, double to, bool animated) {
    if (compiled_.steps.empty() || to <= from) return;
    for (const auto& g : compiled_.steps[step_].gestures) {
        const double gs = g.startMs;
        const double ge = gs + g.durationMs;
        if (gs >= to || ge <= from) continue;
        const double f0 = std::clamp((from - gs) / g.durationMs, 0.0, 1.0);
        const double f1 = std::clamp((to - gs) / g.durationMs, 0.0, 1.0);
        if (f1 > f0) stage_.advance(g, f0, f1, animated);
    }
}

void TutorialPlayer::enterStep(std::size_t step) {
    step_ = step;
    stepTime_ = 0;
    stage_.showStep(compiled_, step_);
}

// Fin d'une etape, comme la maquette validee : on s'arrete seulement si "pause
// apres chaque etape" est cochee (une etape avec un A toi enchaine aussi) ;
// apres la derniere, c'est la fin.
void TutorialPlayer::finishStep() {
    if (step_ + 1 >= compiled_.steps.size()) { state_ = PlayerState::Finished; return; }
    if (pauseAfterEachStep_) { state_ = PlayerState::StepEnd; return; }
    enterStep(step_ + 1);
}

void TutorialPlayer::tick(double dtMs) {
    if (state_ != PlayerState::Playing || compiled_.steps.empty() || dtMs <= 0) return;
    if (stage_.busy()) return;   // la scene rattrape (remise a neuf, gestes en file)
    double remaining = dtMs * speed_;
    while (remaining > 0 && state_ == PlayerState::Playing) {
        const double end = compiled_.steps[step_].durationMs;
        const double to = std::min(end, stepTime_ + remaining);
        // Integration I111 (session 89) : le reste de l'arrondi (7e-15 ms a 100 ms, images de
        // 33,33 ms) ne fait plus avancer stepTime_ : sans ce garde-fou, la boucle ne finit pas.
        if (to <= stepTime_ && stepTime_ < end) break;
        playRange(stepTime_, to, true);
        remaining -= to - stepTime_;
        stepTime_ = to;
        if (stepTime_ >= end) finishStep();  // change l'etape ou l'etat : pas de boucle sans fin
    }
}

void TutorialPlayer::play() {
    if (state_ == PlayerState::Paused) state_ = PlayerState::Playing;
    else if (state_ == PlayerState::StepEnd) next();
    else if (state_ == PlayerState::Finished) restart();
}

void TutorialPlayer::pause() {
    if (state_ == PlayerState::Playing) state_ = PlayerState::Paused;
}

void TutorialPlayer::togglePlay() {
    if (state_ == PlayerState::Playing) pause();
    else if (state_ != PlayerState::ATry) play();
}

// L'ETAT EXACT : une copie neuve du bac a sable, puis tout ce qui precede, sans animation.
void TutorialPlayer::seek(std::size_t step, double ms) {
    if (compiled_.steps.empty()) return;
    if (state_ == PlayerState::ATry) stage_.setInteractive(false);
    const bool wasPlaying = state_ == PlayerState::Playing;
    step = std::min(step, compiled_.steps.size() - 1);
    ms = std::clamp(ms, 0.0, static_cast<double>(compiled_.steps[step].durationMs));
    stage_.reset(compiled_);
    ++resets_;
    // @avant : la preparation du bac a sable (vues, objets), avant l'etape 1.
    for (const auto& g : compiled_.before) stage_.advance(g, 0.0, 1.0, false);
    for (std::size_t i = 0; i < step; ++i)
        for (const auto& g : compiled_.steps[i].gestures) stage_.advance(g, 0.0, 1.0, false);
    enterStep(step);
    playRange(0, ms, false);
    stepTime_ = ms;
    outcome_ = {};
    state_ = wasPlaying ? PlayerState::Playing : PlayerState::Paused;
}

void TutorialPlayer::seekTime(double ms) {
    if (compiled_.steps.empty()) return;
    const auto s = compiled_.stepAt(ms);
    seek(s, ms - compiled_.steps[s].startMs);
}

void TutorialPlayer::previous() {
    if (compiled_.steps.empty()) return;
    if (state_ == PlayerState::StepEnd || state_ == PlayerState::Finished) state_ = PlayerState::Paused;
    if (stepTime_ > 1500 || step_ == 0) seek(step_, 0);
    else seek(step_ - 1, 0);
}

void TutorialPlayer::next() {
    if (compiled_.steps.empty()) return;
    if (state_ != PlayerState::ATry) state_ = PlayerState::Paused;
    if (step_ + 1 < compiled_.steps.size()) {
        seek(step_ + 1, 0);
        state_ = PlayerState::Playing;  // comme la maquette : Suivante relance la lecture
    } else {
        seek(step_, compiled_.steps[step_].durationMs);
        state_ = PlayerState::Finished;
    }
}

void TutorialPlayer::restart() {
    state_ = PlayerState::Paused;
    seek(0, 0);
    if (!compiled_.steps.empty()) state_ = PlayerState::Playing;
}

void TutorialPlayer::setSpeed(double speed) { speed_ = std::clamp(speed, 0.25, 4.0); }

void TutorialPlayer::setPauseAfterEachStep(bool on) { pauseAfterEachStep_ = on; }

void TutorialPlayer::setVariant(std::string_view variant, bool keepStep) {
    compiled_ = tutorial_.compile(variant);
    if (compiled_.steps.empty()) { step_ = 0; stepTime_ = 0; return; }
    if (keepStep) {
        step_ = std::min(step_, compiled_.steps.size() - 1);
        stepTime_ = std::min(stepTime_, static_cast<double>(compiled_.steps[step_].durationMs));
        return;
    }
    if (state_ != PlayerState::Playing) state_ = PlayerState::Paused;
    seek(0, 0);
}

bool TutorialPlayer::startATry() {
    if (!canATry()) return false;
    if (state_ != PlayerState::ATry) state_ = PlayerState::Paused;
    seek(step_, 0);
    stage_.setInteractive(true);
    state_ = PlayerState::ATry;
    outcome_ = {};
    return true;
}

CheckOutcome TutorialPlayer::verify() {
    if (state_ != PlayerState::ATry || !canATry()) return outcome_;
    const TutorialATry aTry = *compiled_.steps[step_].aTry;  // une copie : setVariant recompile
    outcome_ = evaluateATry(aTry, [this](std::string_view path) { return stage_.read(path); });
    if (outcome_.result != CheckOutcome::Result::Ok) return outcome_;
    stage_.setInteractive(false);
    if (!aTry.onOkVariantFrom.empty()) {
        const auto v = stage_.read(aTry.onOkVariantFrom);
        const auto& vs = compiled_.variants;
        if (v && *v != compiled_.variant && std::find(vs.begin(), vs.end(), *v) != vs.end()) {
            const auto kept = outcome_;
            setVariant(*v, true);
            outcome_ = kept;
        }
    }
    stepTime_ = compiled_.steps[step_].durationMs;
    state_ = PlayerState::StepEnd;
    return outcome_;
}

void TutorialPlayer::showMe() {
    if (!canATry()) return;
    if (state_ == PlayerState::ATry) stage_.setInteractive(false);
    state_ = PlayerState::Paused;
    seek(step_, 0);
    state_ = PlayerState::Playing;
}

void TutorialPlayer::retryATry() {
    if (state_ == PlayerState::ATry) stage_.setInteractive(false);
    state_ = PlayerState::Paused;
    (void)startATry();
}

void TutorialPlayer::leaveATry() {
    if (state_ != PlayerState::ATry) return;
    seek(step_, 0);  // seek rend la main au tutoriel (setInteractive(false)), en pause
}

} // namespace help
