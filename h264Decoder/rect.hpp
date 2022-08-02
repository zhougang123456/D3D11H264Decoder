#ifndef RECT_HPP_
#define RECT_HPP_
#include "point.hpp"
#include "size.hpp"
class Rect {
public:
    Rect() = default;
    Rect(int width, int height) : size_(width, height){}
    Rect(int x, int y, int width, int height) : origin_(x, y), size_(width ,height) {}
    Rect(const Size& size) : size_(size) {}
    constexpr const Point& origin() const { return origin_; }
    constexpr const Size& size() const { return size_; }

private:
    Point origin_;
    Size size_;
};

inline bool operator==(const Rect& lhs, const Rect& rhs) {
    return lhs.origin() == rhs.origin() && lhs.size() == rhs.size();
}

inline bool operator!=(const Rect& lhs, const Rect& rhs) {
    return !(lhs == rhs);
}


#endif // RECT_HPP_

