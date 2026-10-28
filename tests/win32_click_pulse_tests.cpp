#include "../src/win32/ClickPulse.hpp"
#include <atomic>
#include <iostream>

namespace {
std::atomic<int> downs = 0, ups = 0, upAttempts = 0;
std::atomic<bool> rejectDown = false, rejectUp = false, blockUp = false;
std::atomic<int> rejectedUps = 0;
UINT WINAPI send(UINT count, LPINPUT input, int size) {
    if (count != 1 || size != sizeof(INPUT))
        return 0;
    if (input->mi.dwFlags == MOUSEEVENTF_LEFTDOWN) {
        if (rejectDown.exchange(false)) {
            SetLastError(ERROR_ACCESS_DENIED);
            return 0;
        }
        ++downs;
    } else if (input->mi.dwFlags == MOUSEEVENTF_LEFTUP) {
        ++upAttempts;
        if (blockUp || rejectUp.exchange(false))
            return 0;
        if (rejectedUps > 0 && rejectedUps.fetch_sub(1) > 0)
            return 0;
        ++ups;
    } else
        return 0;
    return 1;
}
int failures = 0;
void check(bool ok, const char *message) {
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}
std::optional<triggerlab::ClickPulse::Result> await(triggerlab::ClickPulse &pulse) {
    const auto until = triggerlab::Clock::now() + std::chrono::seconds{2};
    do {
        if (auto result = pulse.takeResult())
            return result;
        Sleep(1);
    } while (triggerlab::Clock::now() < until);
    pulse.cancel();
    return {};
}
} // namespace
int main() {
    using namespace std::chrono_literals;
    triggerlab::ClickPulse pulse(send);
    check(!pulse.start(4) && !pulse.start(151) && downs == 0, "invalid durations send nothing");
    rejectDown = true;
    check(!pulse.start(5) && !pulse.takeResult() && ups == 0, "rejected Down needs no Up");
    check(pulse.start(5), "start short press");
    check(!pulse.start(5), "busy start cannot cancel pending release");
    Sleep(100); // Owner/UI thread intentionally does not dispatch messages.
    auto result = await(pulse);
    check(result && result->elapsed >= 5ms && result->elapsed < 100ms && result->releaseAccepted &&
              !result->interrupted,
          "timer releases while owner is stalled");
    for (int i = 0; i < 50; ++i) {
        check(pulse.start(i % 2 ? 30 : 5), "series start");
        result = await(pulse);
        check(result && result->elapsed >= std::chrono::milliseconds{i % 2 ? 30 : 5} &&
                  result->releaseAccepted && !result->interrupted,
              "series completes normally");
        check(downs == ups, "each Down has one Up");
    }
    check(pulse.start(150), "start maximum duration");
    result = await(pulse);
    check(result && result->elapsed >= 150ms, "maximum duration is honored");
    check(pulse.start(150), "start cancellable press");
    pulse.cancel();
    result = pulse.takeResult();
    check(result && result->interrupted && result->requestedMs == 150 && result->releaseAccepted,
          "cancel releases and records interruption");
    const int released = ups;
    pulse.cancel();
    check(!pulse.takeResult() && ups == released, "cancel is idempotent");
    // Cancel close to expiry, then reuse immediately: an old queued callback
    // must never cut the following long pulse short or send another Up.
    for (int i = 0; i < 30; ++i) {
        check(pulse.start(5), "race setup");
        Sleep(4);
        pulse.cancel();
        pulse.takeResult();
        check(pulse.start(30), "restart after cancellation");
        result = await(pulse);
        check(result && result->elapsed >= 30ms && downs == ups,
              "cancelled callback cannot release next pulse");
    }
    const int attempts = upAttempts;
    rejectUp = true;
    check(pulse.start(5), "release failure setup");
    result = await(pulse);
    check(result && result->releaseAccepted && !pulse.pendingRelease() && downs == ups &&
              upAttempts == attempts + 2,
          "successful second Up is acknowledged");
    rejectedUps = 2;
    check(pulse.start(5), "two rejected Ups setup");
    result = await(pulse);
    check(result && !result->releaseAccepted && pulse.pendingRelease() && downs == ups + 1,
          "two failures retain pending release");
    const int pendingDowns = downs;
    check(!pulse.start(5) && downs == pendingDowns, "pending failed release blocks new Down");
    pulse.cancel();
    result = pulse.takeResult();
    check(result && result->releaseAccepted && result->interrupted && !pulse.pendingRelease() && downs == ups,
          "cancel retries pending Up after failures");
    check(pulse.start(5), "restart after recovered release");
    result = await(pulse);
    check(result && result->releaseAccepted && !result->interrupted && downs == ups,
          "recovered instance starts cleanly");
    blockUp = true;
    check(pulse.start(5), "persistent rejection setup");
    result = await(pulse);
    check(result && !result->releaseAccepted && pulse.pendingRelease(),
          "persistent rejection remains pending");
    pulse.cancel();
    result = pulse.takeResult();
    check(result && !result->releaseAccepted && pulse.pendingRelease() && !pulse.start(5),
          "failed cancellation cannot forget a Down");
    blockUp = false;
    pulse.cancel();
    result = pulse.takeResult();
    check(result && result->releaseAccepted && result->interrupted && !pulse.pendingRelease() && downs == ups,
          "recovery after persistent rejection");
    {
        triggerlab::ClickPulse abandoned(send);
        check(abandoned.start(150), "destructor retry setup");
        rejectedUps = 2;
    }
    check(downs == ups, "destructor retries after draining callbacks");
    {
        triggerlab::ClickPulse abandoned(send);
        check(abandoned.start(150), "destructor setup");
    }
    check(downs == ups, "destructor releases pending button");
    std::cout << "downs=" << downs << " ups=" << ups << " failures=" << failures << '\n';
    return failures ? 1 : 0;
}
