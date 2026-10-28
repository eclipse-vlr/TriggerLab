#pragma once
#include "triggerlab/ImageColor.hpp"
#include <windows.h>
namespace triggerlab {
std::optional<ImageColor> showImagePalette(HWND owner, const Frame &image, bool darkTheme = false);
#ifdef TRIGGERLAB_UI_TESTS
bool imagePaletteDialogSmoke();
#endif
} // namespace triggerlab
