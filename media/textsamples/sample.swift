// sample.swift — Swift syntax-highlighting sample for the UltraCanvas demo.
import Foundation

protocol Shape {
    var name: String { get }
    func area() -> Double
}

struct Circle: Shape {
    let radius: Double

    var name: String { "circle" }

    func area() -> Double {
        Double.pi * radius * radius
    }
}

func totalArea(_ shapes: [Shape]) -> Double {
    shapes.reduce(0) { $0 + $1.area() }
}

let shapes: [Shape] = [Circle(radius: 1.0), Circle(radius: 2.5)]

for shape in shapes {
    print(String(format: "%@ -> %.4f", shape.name, shape.area()))
}

print("total = \(totalArea(shapes))")
