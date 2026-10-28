#pragma once
#include <cstdint>
namespace triggerlab {
struct ClickPermission {
    std::uintptr_t selectedWindow = 0, hoverWindow = 0, foregroundWindow = 0;
    std::uint32_t selectedProcess = 0, currentProcess = 0;
    bool exists = false;
    [[nodiscard]] bool allowed() const noexcept {
        return exists && selectedWindow && selectedProcess && selectedProcess == currentProcess &&
               hoverWindow == selectedWindow && foregroundWindow == selectedWindow;
    }
};
} // namespace triggerlab
