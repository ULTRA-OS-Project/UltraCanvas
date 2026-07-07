// sample.cs — C# syntax-highlighting sample for the UltraCanvas demo.
using System;
using System.Collections.Generic;
using System.Linq;

namespace UltraCanvas.Demo
{
    public interface IShape
    {
        double Area();
        string Name { get; }
    }

    public sealed class Circle : IShape
    {
        private readonly double _radius;

        public Circle(double radius) => _radius = radius;

        public string Name => "circle";
        public double Area() => Math.PI * _radius * _radius;
    }

    public static class Program
    {
        public static void Main()
        {
            var shapes = new List<IShape>
            {
                new Circle(1.0),
                new Circle(2.5),
            };

            double total = shapes.Sum(s => s.Area());
            foreach (var s in shapes)
                Console.WriteLine($"{s.Name} -> {s.Area():F4}");

            Console.WriteLine($"total = {total:F4}");
        }
    }
}
