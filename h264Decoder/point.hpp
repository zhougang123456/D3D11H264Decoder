#ifndef POINT_HPP_
#define POINT_HPP_

class Point {
public:
    Point() : x_(0), y_(0){}
    Point(int x, int y) : x_(x), y_(y){}
    constexpr int x() const { return x_; }
    constexpr int y() const { return y_; }
private:
    int x_;
    int y_;
};

inline bool operator==(const Point& lhs, const Point& rhs)
{
    return lhs.x() == rhs.x() && lhs.y() == rhs.y();
}

inline bool operator!=(const Point& lhs, const Point& rhs) {
    return !(lhs == rhs);
}

#endif // POINT_HPP_
