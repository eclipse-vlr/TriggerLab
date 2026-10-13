#pragma once
#include "triggerlab/Frame.hpp"
#include "triggerlab/Region.hpp"
#include <memory>
#include <optional>
namespace triggerlab {
enum class CaptureStatus { Ok, InvalidRegion, DesktopUnavailable, AllocationFailed, CopyFailed, CursorMoved };
struct CapturePoint {int x=0,y=0;};
struct CaptureSample {
    CaptureStatus status=CaptureStatus::DesktopUnavailable;
    Region region{};
    std::optional<Frame> frame; // empty on every failure; never exposes a stale frame
    std::optional<CapturePoint> cursorPosition;
    std::optional<Pixel> cursorPixel; // real cursor pixel; absent on failure or non-cursor capture
    bool allBlack=false; // may be legitimate black pixels or protected content
};
// Windows-only implementation. Single-thread owned; construction does not capture.
class ScreenCapture {
public:
    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&)=delete;
    ScreenCapture& operator=(const ScreenCapture&)=delete;
    CaptureSample capture(Region requested);
    CaptureSample captureCursor(int side=17);
    void reset() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
