// sample.ts — TypeScript syntax-highlighting sample for the UltraCanvas demo.

interface Shape {
    readonly name: string;
    area(): number;
}

type Color = "red" | "green" | "blue";

class Circle implements Shape {
    constructor(public readonly radius: number, private color: Color = "blue") {}

    get name(): string {
        return "circle";
    }

    area(): number {
        return Math.PI * this.radius ** 2;
    }
}

function totalArea(shapes: readonly Shape[]): number {
    return shapes.reduce((sum, s) => sum + s.area(), 0);
}

const shapes: Shape[] = [new Circle(1), new Circle(2.5, "red")];
console.log(`total area = ${totalArea(shapes).toFixed(3)}`);

const enum Direction { Up, Down, Left, Right }
const move = (d: Direction): string => Direction[d];
console.log(move(Direction.Left));
