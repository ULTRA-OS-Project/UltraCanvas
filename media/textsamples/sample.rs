// sample.rs — Rust syntax-highlighting sample for the UltraCanvas demo.
use std::f64::consts::PI;

trait Shape {
    fn area(&self) -> f64;
    fn name(&self) -> &str;
}

struct Circle {
    radius: f64,
}

impl Shape for Circle {
    fn area(&self) -> f64 {
        PI * self.radius * self.radius
    }

    fn name(&self) -> &str {
        "circle"
    }
}

fn total_area(shapes: &[Box<dyn Shape>]) -> f64 {
    shapes.iter().map(|s| s.area()).sum()
}

fn main() {
    let shapes: Vec<Box<dyn Shape>> = vec![
        Box::new(Circle { radius: 1.0 }),
        Box::new(Circle { radius: 2.5 }),
    ];

    for shape in &shapes {
        println!("{} -> {:.4}", shape.name(), shape.area());
    }
    println!("total = {:.4}", total_area(&shapes));
}
