#pragma once
namespace triggerlab {
struct InputAuthorization {
    bool colorReady, verified, dirty;
    constexpr bool allows(bool dryRun) const noexcept {
        return colorReady && !dirty && (dryRun || verified);
    }
};
} // namespace triggerlab
