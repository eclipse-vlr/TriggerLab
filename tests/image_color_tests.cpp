#include "triggerlab/ImageColor.hpp"
#include <cstdlib>
#include <iostream>
void check(bool value, const char *message) {
    if (!value) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
int main() {
    using namespace triggerlab;
    Frame image(10, 1);
    image.clear({0, 0, 255, 255});
    for (int x = 0; x < 4; ++x)
        image.at(x, 0) = {255, 0, 0, 255};
    auto selected = dominantImageColor(image);
    check(selected && selected->color.red == 255 && selected->color.blue == 0 && selected->coverage == .6,
          "dominant red, not averaged purple");
    image.clear({0, 0, 255, 0});
    image.at(0, 0) = {19, 67, 231, 255};
    selected = dominantImageColor(image);
    check(selected && selected->color.blue == 19 && selected->color.red == 231 && selected->coverage == 1,
          "transparent background ignored, BGRA preserved");
    image.clear({0, 0, 0, 127});
    check(!dominantImageColor(image), "no opaque color rejected");
    image.at(0, 0) = {255, 255, 255, 128};
    check(dominantImageColor(image)->color.red == 255, "alpha threshold inclusive");
    Frame varied(3, 1);
    varied.at(0, 0) = {0, 0, 248, 255};
    varied.at(1, 0) = {0, 0, 251, 255};
    varied.at(2, 0) = {0, 0, 255, 255};
    check(dominantImageColor(varied)->color.red == 251, "nearby values grouped, representative actual pixel");
    Frame tied(2, 1);
    tied.at(0, 0) = {255, 0, 0, 255};
    tied.at(1, 0) = {0, 0, 255, 255};
    check(dominantImageColor(tied)->color.blue == 255, "ties deterministic");
    Frame crop(10, 2);
    crop.clear({255, 255, 255, 255});
    crop.at(8, 0) = {0, 0, 255, 255};
    crop.at(9, 0) = {0, 0, 255, 255};
    crop.at(8, 1) = {255, 0, 0, 255};
    crop.at(9, 1) = {255, 0, 0, 0};
    auto palette = imagePalette(crop, {8, 0, 2, 2});
    check(palette.size() == 2 && palette[0].color.red == 255 && palette[0].color.green == 0 &&
              palette[0].coverage == 2. / 3,
          "crop ignores dominant white background and transparency");
    check(imagePalette(crop, {99, 99, 1, 1}).empty() && imagePalette(crop, {0, 0, 10, 2}, 0).empty(),
          "empty crop and zero limit");
    check(imagePalette(crop, {0, 0, 10, 2}, 1).size() == 1, "palette size capped");
    auto selection = imageSelection(100, 50, {20, 60, 200, 100}, 219, 159, 20, 60);
    check(selection && selection->x == 0 && selection->y == 0 && selection->width == 100 &&
              selection->height == 50,
          "reverse drag covers full source");
    selection = imageSelection(100, 50, {20, 60, 200, 100}, -100, -100, 10000, 10000);
    check(selection && selection->width == 100 && selection->height == 50, "drag outside image clipped");
    selection = imageSelection(2, 1, {20, 60, 720, 360}, 20, 60, 20, 60);
    check(selection && selection->width == 1 && selection->height == 1,
          "click picks one source pixel in magnified image");
    check(!imageSelection(100, 50, {20, 60, 0, 100}, 0, 0, 1, 1), "invalid viewport rejected");
    std::cout << "PASS: dominant image color and transparency\n";
}
