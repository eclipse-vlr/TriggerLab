#pragma once
#include <windows.h>

namespace triggerlab::win32::control {
inline constexpr int startCapture=1;
inline constexpr int stop=2;
inline constexpr int applyColor=3;
inline constexpr int chooseColor=4;
inline constexpr int pickCursorColor=5;
inline constexpr int saveColor=6;
inline constexpr int chooseWindow=7;
inline constexpr int enableTrigger=8;
inline constexpr int importImage=10;
inline constexpr int editImage=14;
inline constexpr int detectionMode=15;
inline constexpr int dryRun=16;
inline constexpr int saveParameters=17;
inline constexpr int findKovaaks=18;
inline constexpr int findAimlabs=19;
inline constexpr int pickAimColor=20;
inline constexpr int toggleTheme=21;
inline constexpr int guidedAction=22;
inline constexpr int toggleAdvanced=23;
inline constexpr int saveAll=24;
inline constexpr int toggleProfiles=25;
inline constexpr int loadProfile=26;
inline constexpr int saveProfile=27;
inline constexpr int profileChoice=28;
inline constexpr int profileName=29;
inline constexpr int headerStatus=30;
inline constexpr int targetReference=31;
inline constexpr int backgroundReference=32;
inline constexpr int qualityInfo=33;
inline constexpr int toggleDetails=34;
inline constexpr int homeHint=35;
inline constexpr int homeSummary=36;
inline constexpr int firstParameterHelp=100;
inline constexpr int colorToleranceHelp=108;
}

namespace triggerlab::win32::timer {
inline constexpr UINT_PTR capture=1,selection=2,recovery=3;
}
namespace triggerlab::win32::hotkey {
inline constexpr int stop=9;
}
