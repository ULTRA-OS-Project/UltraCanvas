<?php
// sample.php — PHP syntax-highlighting sample for the UltraCanvas demo.
declare(strict_types=1);

interface Shape
{
    public function area(): float;
    public function name(): string;
}

final class Circle implements Shape
{
    public function __construct(private float $radius) {}

    public function area(): float
    {
        return M_PI * $this->radius ** 2;
    }

    public function name(): string
    {
        return 'circle';
    }
}

$shapes = [new Circle(1.0), new Circle(2.5)];

$total = array_reduce(
    $shapes,
    static fn (float $carry, Shape $s): float => $carry + $s->area(),
    0.0
);

foreach ($shapes as $shape) {
    printf("%s -> %.4f\n", $shape->name(), $shape->area());
}

printf("total = %.4f\n", $total);
