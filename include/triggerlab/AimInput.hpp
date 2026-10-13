#pragma once
namespace triggerlab {
class AimInput {
public:
    void move(int x, int y) noexcept { inside_=x>=0 && y>=0 && x<960 && y<540; if(inside_) {x_=x; y_=y;} }
    void press() noexcept { held_=inside_; }
    void release() noexcept { held_=false; }
    void leave() noexcept { inside_=false; }
    void center() noexcept { x_=480; y_=270; held_=false; }
    [[nodiscard]] bool active() const noexcept {return inside_ && held_;}
    [[nodiscard]] bool inside() const noexcept {return inside_;}
    [[nodiscard]] int x() const noexcept {return x_;}
    [[nodiscard]] int y() const noexcept {return y_;}
private:
    int x_=480,y_=270;
    bool inside_=false,held_=false;
};
}
