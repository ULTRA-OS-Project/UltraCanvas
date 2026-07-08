// sample.js — JavaScript syntax-highlighting sample for the UltraCanvas demo.
"use strict";

const GRAVITY = 9.81;

class Vector2 {
    constructor(x = 0, y = 0) {
        this.x = x;
        this.y = y;
    }

    get lengthSquared() {
        return this.x ** 2 + this.y ** 2;
    }

    add(other) {
        return new Vector2(this.x + other.x, this.y + other.y);
    }
}

function simulate(steps, dt = 0.016) {
    let position = new Vector2(0, 100);
    let velocity = new Vector2(0, 0);
    const trail = [];

    for (let i = 0; i < steps; i++) {
        velocity = velocity.add(new Vector2(0, -GRAVITY * dt));
        position = position.add(new Vector2(velocity.x * dt, velocity.y * dt));
        trail.push({ ...position });
    }
    return trail;
}

const points = simulate(5);
points.forEach((p, i) => console.log(`step ${i}: y=${p.y.toFixed(2)}`));

export { Vector2, simulate };
