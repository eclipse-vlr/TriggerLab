#include "triggerlab/Region.hpp"
#include "triggerlab/Frame.hpp"
#include <cstdlib>
#include <iostream>
#include <limits>
void check(bool c,const char* why) {if(!c) {std::cerr<<why<<'\n';std::exit(1);}}
int main() {
    using namespace triggerlab;
    Region desktop{-1920,-1080,3840,2160};
    auto center=cursorRegion(-100,-100,17,desktop);
    check(center && center->x==-108 && center->width==17,"negative physical coordinates");
    auto edge=cursorRegion(-1920,-1080,17,desktop);
    check(edge && edge->width==9 && edge->height==9 && edge->x==-1920,"top left clipping");
    auto last=cursorRegion(1919,1079,17,desktop);
    check(last && last->width==9 && last->height==9,"bottom right clipping");
    check(!cursorRegion(1920,0,17,desktop),"outside half open desktop");
    for(int side:{0,-1,2,4096,5000}) check(!cursorRegion(0,0,side,desktop),"invalid side");
    check(!intersectRegion({0,0,10,10},{10,0,10,10}),"touching is empty");
    check(!intersectRegion({0,0,-1,10},desktop),"negative extent");
    check(!validRegion({2147483647,0,17,17}),"int overflow blocked");
    check(!cursorRegion(std::numeric_limits<std::int64_t>::min(),0,17,desktop),"extreme cursor blocked");
    check(!cursorRegion(0,0,17,{std::numeric_limits<std::int64_t>::min(),0,17,17}),"extreme bounds blocked");
    auto clipped=intersectRegion({-2,-2,4,4},{0,0,10,10}); check(clipped && clipped->width==2,"intersection size");
    Pixel pixel{3,5,7,255}; const auto* bytes=reinterpret_cast<const unsigned char*>(&pixel);
    check(bytes[0]==3 && bytes[1]==5 && bytes[2]==7 && bytes[3]==255,"BGRA memory order");
    std::cout<<"PASS: capture geometry and BGRA\n";
}
