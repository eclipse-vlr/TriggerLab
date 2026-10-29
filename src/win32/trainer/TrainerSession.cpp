#include "trainer/TrainerWindow.hpp"

namespace triggerlab::win32 {
void TrainerWindow::resetSession() {
    holdingSpace_ = false;
    centerAim();
    engine_.reset();
    diagnostics_.reset();
    simulation_.reset();
    simulation_.render(frame_);
    lastDetection_ = {};
    lastResult_ = {};
    previousTick_ = triggerlab::Clock::now();
}

void TrainerWindow::togglePause() {
    paused_ = !paused_;
    holdingSpace_ = false;
    mouseUp();
    lastResult_ = engine_.step({false, false}, triggerlab::Clock::now());
    diagnostics_.observe({false, false}, lastResult_, triggerlab::Clock::now());
    SetWindowTextW(pauseControl_, tr(paused_ ? L"Resume scene (P)" : L"Pause scene (P)").c_str());
}

void TrainerWindow::onFocusLost() noexcept {
    holdingSpace_ = false;
    aim_.release();
    if (GetCapture() == window_)
        ReleaseCapture();
}

void TrainerWindow::mouseMove(int x, int y) noexcept {
    aim_.move(MulDiv(x + scrollX_, 96, dpi_), MulDiv(y + scrollY_, 96, dpi_));
    TRACKMOUSEEVENT tracking{sizeof(TRACKMOUSEEVENT), TME_LEAVE, window_, 0};
    TrackMouseEvent(&tracking);
}

void TrainerWindow::mouseDown(int x, int y) noexcept {
    mouseMove(x, y);
    if (!aim_.inside())
        return;
    SetFocus(window_);
    aim_.press();
    if (!paused_)
        SetCapture(window_);
}

void TrainerWindow::mouseUp() noexcept {
    aim_.release();
    if (GetCapture() == window_)
        ReleaseCapture();
}

void TrainerWindow::mouseLeave() noexcept {
    aim_.leave();
}

void TrainerWindow::mouseCaptureLost() noexcept {
    aim_.release();
}

void TrainerWindow::centerAim() noexcept {
    mouseUp();
    aim_.center();
}

bool TrainerWindow::active() const noexcept {
    return !paused_ && (holdingSpace_ || aim_.active());
}

void TrainerWindow::onKeyDown(WPARAM key, LPARAM lparam) {
    if (key == VK_SPACE) {
        holdingSpace_ = !paused_;
        return;
    }
    if (key == VK_ESCAPE) {
        DestroyWindow(window_);
        return;
    }
    if ((lparam & (1LL << 30)) != 0)
        return; // avoid key-repeat toggles
    if (key == 'P')
        togglePause();
    else if (key == 'R') {
        resetSession();
    } else if (key == VK_UP || key == VK_DOWN) {
        auto config = engine_.config();
        const int change = (key == VK_UP) ? 25 : -25;
        config.cooldown += std::chrono::milliseconds{change};
        engine_.setConfig(config);
    } else if (key == VK_RIGHT || key == VK_LEFT) {
        auto config = detector_.config();
        config.roiRadius += (key == VK_RIGHT) ? 1 : -1;
        detector_.setConfig(config);
    }
    if (key == VK_UP || key == VK_DOWN || key == VK_LEFT || key == VK_RIGHT) {
        syncControls();
        save();
    }
    InvalidateRect(window_, nullptr, FALSE);
}

void TrainerWindow::onKeyUp(WPARAM key) noexcept {
    if (key == VK_SPACE)
        holdingSpace_ = false;
}

void TrainerWindow::tick() {
    const auto now = triggerlab::Clock::now();
    const double dt = std::chrono::duration<double>(now - previousTick_).count();
    previousTick_ = now;
    if (dt > 0.0) {
        const double instantaneousFps = 1.0 / dt;
        fps_ = fps_ == 0.0 ? instantaneousFps : fps_ * 0.9 + instantaneousFps * 0.1;
    }

    if (!paused_)
        simulation_.advance(dt);
    simulation_.render(frame_);
    lastDetection_ = detector_.detect(frame_, aim_.x(), aim_.y());
    lastResult_ = engine_.step({active(), lastDetection_.found}, now);
    diagnostics_.observe({active(), lastDetection_.found}, lastResult_, now);
    if (lastResult_.fired)
        simulation_.onSimulatedShot(aim_.x(), aim_.y());
    InvalidateRect(window_, nullptr, FALSE);
}
} // namespace triggerlab::win32
