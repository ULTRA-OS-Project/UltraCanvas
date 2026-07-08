// sample.cpp — C++ syntax-highlighting sample for the UltraCanvas demo.
#include <iostream>
#include <string>
#include <vector>
#include <algorithm>

namespace demo {

// A small immutable value type.
struct Point {
    double x = 0.0;
    double y = 0.0;

    double LengthSquared() const { return x * x + y * y; }
};

template <typename T>
T Clamp(T value, T lo, T hi) {
    return std::max(lo, std::min(value, hi));
}

class Shape {
public:
    explicit Shape(std::string name) : name_(std::move(name)) {}
    virtual ~Shape() = default;

    virtual double Area() const = 0;
    const std::string& Name() const { return name_; }

private:
    std::string name_;
};

class Circle : public Shape {
public:
    Circle(double radius) : Shape("circle"), radius_(radius) {}
    double Area() const override { return 3.14159265358979 * radius_ * radius_; }

private:
    double radius_;
};

} // namespace demo

int main() {
    using namespace demo;

    std::vector<Point> points{{1.0, 2.0}, {3.0, 4.0}, {-5.0, 0.5}};
    std::sort(points.begin(), points.end(), [](const Point& a, const Point& b) {
        return a.LengthSquared() < b.LengthSquared();
    });

    Circle c(2.5);
    std::cout << "Shape '" << c.Name() << "' has area " << c.Area() << '\n';

    for (const auto& p : points) {
        std::cout << "(" << p.x << ", " << p.y << ")\n";
    }
    return 0;
}
