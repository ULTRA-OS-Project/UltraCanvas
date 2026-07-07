"""sample.py — Python syntax-highlighting sample for the UltraCanvas demo."""

from __future__ import annotations

from dataclasses import dataclass
from math import pi
from typing import Iterable


@dataclass(frozen=True)
class Point:
    x: float = 0.0
    y: float = 0.0

    @property
    def length_squared(self) -> float:
        return self.x * self.x + self.y * self.y


class Shape:
    def __init__(self, name: str) -> None:
        self.name = name

    def area(self) -> float:
        raise NotImplementedError


class Circle(Shape):
    def __init__(self, radius: float) -> None:
        super().__init__("circle")
        self.radius = radius

    def area(self) -> float:
        return pi * self.radius ** 2


def nearest_to_origin(points: Iterable[Point]) -> Point:
    return min(points, key=lambda p: p.length_squared)


def main() -> None:
    points = [Point(1, 2), Point(3, 4), Point(-5, 0.5)]
    print(f"nearest: {nearest_to_origin(points)}")

    circle = Circle(2.5)
    print(f"Shape {circle.name!r} has area {circle.area():.4f}")

    for i, p in enumerate(points, start=1):
        print(f"{i}: ({p.x}, {p.y})")


if __name__ == "__main__":
    main()
