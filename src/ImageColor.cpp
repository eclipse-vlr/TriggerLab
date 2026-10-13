#include "triggerlab/ImageColor.hpp"
#include <limits>

namespace triggerlab {
std::optional<ImageColor> dominantImageColor(const Frame& image) {
    struct Bucket {std::uint64_t count=0,red=0,green=0,blue=0;};
    std::vector<Bucket> buckets(32768);
    auto key=[](Pixel p) {return (unsigned(p.red>>3)<<10) | (unsigned(p.green>>3)<<5) | unsigned(p.blue>>3);};
    std::uint64_t total=0;
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const auto p=image.at(x,y);if(p.alpha<128) continue;
        auto& bucket=buckets[key(p)];++bucket.count;++total;
        bucket.red+=p.red;bucket.green+=p.green;bucket.blue+=p.blue;
    }
    if(!total) return {};
    unsigned winner=0;
    for(unsigned i=1;i<buckets.size();++i) if(buckets[i].count>buckets[winner].count) winner=i;
    const auto& bucket=buckets[winner];
    const double r=double(bucket.red)/double(bucket.count),g=double(bucket.green)/double(bucket.count),b=double(bucket.blue)/double(bucket.count);
    double closest=std::numeric_limits<double>::infinity();Pixel selected{};
    for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) {
        const auto p=image.at(x,y);if(p.alpha<128 || key(p)!=winner) continue;
        const double distance=(p.red-r)*(p.red-r)+(p.green-g)*(p.green-g)+(p.blue-b)*(p.blue-b);
        if(distance<closest) {closest=distance;selected=p;selected.alpha=255;}
    }
    return ImageColor{selected,double(bucket.count)/double(total)};
}
}
