// sample.java — Java syntax-highlighting sample for the UltraCanvas demo.
package com.ultracanvas.demo;

import java.util.List;
import java.util.ArrayList;

public final class ShapeDemo {

    interface Shape {
        double area();
        String name();
    }

    static final class Circle implements Shape {
        private final double radius;

        Circle(double radius) {
            this.radius = radius;
        }

        @Override
        public double area() {
            return Math.PI * radius * radius;
        }

        @Override
        public String name() {
            return "circle";
        }
    }

    public static void main(String[] args) {
        List<Shape> shapes = new ArrayList<>();
        shapes.add(new Circle(1.0));
        shapes.add(new Circle(2.5));

        double total = 0.0;
        for (Shape s : shapes) {
            System.out.printf("%s -> %.4f%n", s.name(), s.area());
            total += s.area();
        }
        System.out.println("total = " + total);
    }
}
