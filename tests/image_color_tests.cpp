#include "triggerlab/ImageColor.hpp"
#include <cstdlib>
#include <iostream>
void check(bool value,const char* message) {if(!value) {std::cerr<<message<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;
    Frame image(10,1);image.clear({0,0,255,255});
    for(int x=0;x<4;++x) image.at(x,0)={255,0,0,255};
    auto selected=dominantImageColor(image);
    check(selected && selected->color.red==255 && selected->color.blue==0 && selected->coverage==.6,"dominant red, not averaged purple");
    image.clear({0,0,255,0});image.at(0,0)={19,67,231,255};
    selected=dominantImageColor(image);
    check(selected && selected->color.blue==19 && selected->color.red==231 && selected->coverage==1,"transparent background ignored, BGRA preserved");
    image.clear({0,0,0,127});check(!dominantImageColor(image),"no opaque color rejected");
    image.at(0,0)={255,255,255,128};check(dominantImageColor(image)->color.red==255,"alpha threshold inclusive");
    Frame varied(3,1);varied.at(0,0)={0,0,248,255};varied.at(1,0)={0,0,251,255};varied.at(2,0)={0,0,255,255};
    check(dominantImageColor(varied)->color.red==251,"nearby values grouped, representative actual pixel");
    Frame tied(2,1);tied.at(0,0)={255,0,0,255};tied.at(1,0)={0,0,255,255};
    check(dominantImageColor(tied)->color.blue==255,"ties deterministic");
    std::cout<<"PASS: dominant image color and transparency\n";
}
