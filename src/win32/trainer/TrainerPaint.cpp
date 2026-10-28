#include "trainer/TrainerWindow.hpp"
#include "triggerlab/Version.hpp"
#include <algorithm>
#include <cmath>

namespace triggerlab::win32 {
void TrainerWindow::paint(HDC screen, const RECT &client) {
    if (!backBuffer_.ensure(screen, client.right - client.left, client.bottom - client.top))
        return;
    const HDC dc = backBuffer_.dc();
    BITMAPINFO bitmap{};
    bitmap.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmap.bmiHeader.biWidth = frame_.width();
    bitmap.bmiHeader.biHeight = -frame_.height(); // top-down pixel buffer
    bitmap.bmiHeader.biPlanes = 1;
    bitmap.bmiHeader.biBitCount = 32;
    bitmap.bmiHeader.biCompression = BI_RGB;

    SetDCBrushColor(dc, RGB(31, 38, 48));
    FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    RECT panel{960, 0, client.right, client.bottom};
    FillRect(dc, &panel, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(225, 230, 238));
    drawText(dc, 976, 18, tr(L"SETTINGS / SCENE") + L" · v" + std::wstring(triggerlab::wideVersion));
    const int displayWidth = std::min(960L, client.right - client.left);
    const int displayHeight = std::min(540L, client.bottom - client.top);
    if (displayWidth <= 0 || displayHeight <= 0)
        return;
    SetStretchBltMode(dc, COLORONCOLOR);
    StretchDIBits(dc, 0, 0, displayWidth, displayHeight, 0, 0, frame_.width(), frame_.height(), frame_.data(),
                  &bitmap, DIB_RGB_COLORS, SRCCOPY);

    const int crossX = aim_.x();
    const int crossY = aim_.y();
    HGDIOBJ previousPen = SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, lastDetection_.found ? RGB(255, 225, 70) : RGB(226, 241, 247));
    const int innerGap = std::max(3, displayWidth / 280);
    const int outerGap = std::max(15, displayWidth / 62);
    MoveToEx(dc, crossX - outerGap, crossY, nullptr);
    LineTo(dc, crossX - innerGap, crossY);
    MoveToEx(dc, crossX + innerGap, crossY, nullptr);
    LineTo(dc, crossX + outerGap, crossY);
    MoveToEx(dc, crossX, crossY - outerGap, nullptr);
    LineTo(dc, crossX, crossY - innerGap);
    MoveToEx(dc, crossX, crossY + innerGap, nullptr);
    LineTo(dc, crossX, crossY + outerGap);
    SelectObject(dc, previousPen);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, RGB(246, 246, 252));
    const int fontHeight = 18;
    const int textX = 18;
    drawText(dc, textX, 18,
             tr(L"TRIGGERLAB  |  LOCAL PRACTICE SANDBOX") + L" · v" + std::wstring(triggerlab::wideVersion));
    drawText(dc, textX, 18 + fontHeight,
             tr(L"MOUSE: aim | HOLD LMB / SPACE: arm | RMB: center | P: pause | R: reset"));
    drawText(dc, textX, 18 + fontHeight * 2, tr(L"UP/DOWN: cooldown  |  LEFT/RIGHT: detection ROI"));

    const int textY = 18 + fontHeight * 4;
    SetTextColor(dc, active() ? RGB(130, 255, 176) : RGB(240, 190, 156));
    drawText(dc, textX, textY,
             tr(L"Engine: ") + std::wstring(tr(stateLabel(lastResult_.state))) +
                 (active() ? tr(L" (holding)") : tr(L" (inactive)")));
    SetTextColor(dc, RGB(235, 240, 245));
    const int coveragePercent = static_cast<int>(std::lround(lastDetection_.coverage * 100.0));
    drawText(dc, textX, textY + fontHeight,
             tr(L"Target: ") + std::wstring(lastDetection_.found ? tr(L"YES") : tr(L"NO")) +
                 tr(L"  |  Coverage: ") + std::to_wstring(coveragePercent) + tr(L"%"));
    drawText(dc, textX, textY + fontHeight * 2,
             tr(L"Simulated shots: ") + std::to_wstring(lastResult_.shotCount) + tr(L"  |  Cooldown: ") +
                 std::to_wstring(engine_.config().cooldown.count()) + tr(L"ms"));
    drawText(dc, textX, textY + fontHeight * 3,
             tr(L"ROI radius: ") + std::to_wstring(detector_.config().roiRadius) + tr(L"px  |  FPS: ") +
                 std::to_wstring(static_cast<int>(std::lround(fps_))));

    drawText(dc, textX, textY + fontHeight * 4,
             tr(L"Stable frames: ") + std::to_wstring(engine_.config().stableFrames));
    auto metric = [](std::optional<double> value) {
        if (!value)
            return std::wstring(L"--");
        return std::to_wstring(static_cast<int>(std::lround(*value)));
    };
    drawText(dc, textX, textY + fontHeight * 5,
             tr(L"Detection to shot: P50 ") + metric(diagnostics_.percentile(.50)) + tr(L" ms / P95 ") +
                 metric(diagnostics_.percentile(.95)) + tr(L" ms | Samples: ") +
                 std::to_wstring(diagnostics_.sampleCount()));
    drawText(dc, textX, textY + fontHeight * 6,
             std::wstring(tr(L"Scene: ")) + tr(sceneLabel(simulation_.config().profile)) + tr(L" | Speed: ") +
                 formatNumber(simulation_.config().speed) + (paused_ ? tr(L" | PAUSED") : tr(L" | RUNNING")));
    drawText(dc, textX, textY + fontHeight * 7,
             tr(L"Recent events: ") + std::to_wstring(diagnostics_.events().size()));
    int eventY = displayHeight - 145;
    const auto &events = diagnostics_.events();
    for (std::size_t i = events.size() > 3 ? events.size() - 3 : 0; i < events.size(); ++i) {
        const auto &event = events[i];
        drawText(dc, textX, eventY,
                 L"+" + std::to_wstring(static_cast<int>(event.elapsedMs)) + tr(L"ms ") +
                     tr(stateLabel(event.state)) + (event.fired ? tr(L" SIMULATED SHOT") : L"") +
                     tr(L" | target ") + (event.targetVisible ? tr(L"YES") : tr(L"NO")));
        eventY += fontHeight;
    }
    drawText(dc, textX, displayHeight - 52, tr(configStatus_));
    if (simulation_.isHitFlashActive()) {
        SetTextColor(dc, RGB(123, 255, 170));
        drawText(dc, crossX + 22, crossY - 55, tr(L"SIMULATED HIT"));
    }
    SetTextColor(dc, RGB(180, 192, 202));
    drawText(dc, textX, displayHeight - 28, tr(L"Simulated shots only | Local scene | No external input"));
    BitBlt(screen, 0, 0, client.right - client.left, client.bottom - client.top, dc, 0, 0, SRCCOPY);
}
} // namespace triggerlab::win32
