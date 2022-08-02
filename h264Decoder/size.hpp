#ifndef SIZE_HPP_
#define SIZE_HPP_
class Size {
public:
    Size() : width_(0), height_(0) {}
    Size(int width, int height) : width_(width), height_(height) {}
    int width() const { return width_; }
    int height() const { return height_; }
    bool IsEmpty() const { return !width() || !height(); }
private:
    int width_;
    int height_;
};

inline bool operator==(const Size& lhs, const Size& rhs)
{
    return lhs.width() == rhs.width() && lhs.height() == rhs.height();
}

inline bool operator!=(const Size& lhs, const Size& rhs) {
    return !(lhs == rhs);
}

#endif // SIZE_HPP_
