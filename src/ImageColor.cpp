#include "triggerlab/ImageColor.hpp"
#include <limits>

namespace triggerlab {
std::vector<ImageColor> imagePalette(const Frame& image,Region region,std::size_t maximum) {
    const auto clipped=intersectRegion(region,{0,0,image.width(),image.height()});
    if(!clipped || maximum==0) return {};
    maximum=std::min<std::size_t>(maximum,5);
    struct Bucket {std::uint64_t count=0,red=0,green=0,blue=0;};
    std::vector<Bucket> buckets(32768);
    auto key=[](Pixel p) {return (unsigned(p.red>>3)<<10) | (unsigned(p.green>>3)<<5) | unsigned(p.blue>>3);};
    std::uint64_t total=0;
    for(int y=static_cast<int>(clipped->y);y<clipped->y+clipped->height;++y) for(int x=static_cast<int>(clipped->x);x<clipped->x+clipped->width;++x) {
        const auto p=image.at(x,y);if(p.alpha<128) continue;
        auto& bucket=buckets[key(p)];++bucket.count;++total;
        bucket.red+=p.red;bucket.green+=p.green;bucket.blue+=p.blue;
    }
    if(!total) return {};
    std::vector<unsigned> order;
    for(unsigned i=0;i<buckets.size();++i) if(buckets[i].count) order.push_back(i);
    std::sort(order.begin(),order.end(),[&](unsigned a,unsigned b) {return buckets[a].count!=buckets[b].count?buckets[a].count>buckets[b].count:a<b;});
    order.resize(std::min(order.size(),maximum));
    std::vector<int> rank(32768,-1);
    std::vector<double> closest(order.size(),std::numeric_limits<double>::infinity());
    std::vector<ImageColor> result(order.size());
    for(std::size_t i=0;i<order.size();++i) {rank[order[i]]=static_cast<int>(i);result[i].coverage=double(buckets[order[i]].count)/double(total);}
    for(int y=static_cast<int>(clipped->y);y<clipped->y+clipped->height;++y) for(int x=static_cast<int>(clipped->x);x<clipped->x+clipped->width;++x) {
        const auto p=image.at(x,y);if(p.alpha<128) continue;
        const int i=rank[key(p)];if(i<0) continue;
        const auto& bucket=buckets[key(p)];
        const double r=double(bucket.red)/double(bucket.count),g=double(bucket.green)/double(bucket.count),b=double(bucket.blue)/double(bucket.count);
        const double distance=(p.red-r)*(p.red-r)+(p.green-g)*(p.green-g)+(p.blue-b)*(p.blue-b);
        if(distance<closest[i]) {closest[i]=distance;result[i].color=p;result[i].color.alpha=255;}
    }
    return result;
}
std::optional<ImageColor> dominantImageColor(const Frame& image) {
    auto palette=imagePalette(image,{0,0,image.width(),image.height()},1);
    if(palette.empty()) return {};
    return palette.front();
}
std::optional<Region> imageSelection(int width,int height,Region display,int x1,int y1,int x2,int y2) noexcept {
    if(width<1 || height<1 || width>4096 || height>4096 || !validRegion(display)) return {};
    auto convert=[](int value,std::int64_t origin,std::int64_t extent,int size) {
        return static_cast<int>(std::clamp<std::int64_t>(value-origin,0,extent-1)*size/extent);
    };
    const int left=convert(std::min(x1,x2),display.x,display.width,width),right=convert(std::max(x1,x2),display.x,display.width,width);
    const int top=convert(std::min(y1,y2),display.y,display.height,height),bottom=convert(std::max(y1,y2),display.y,display.height,height);
    // A click selects the entire displayed pixel, including its source pixels.
    const int endX=static_cast<int>((std::clamp<std::int64_t>(std::max(x1,x2)-display.x,0,display.width-1)+1)*width/display.width);
    const int endY=static_cast<int>((std::clamp<std::int64_t>(std::max(y1,y2)-display.y,0,display.height-1)+1)*height/display.height);
    return Region{left,top,std::max(right+1,endX)-left,std::max(bottom+1,endY)-top};
}
}
