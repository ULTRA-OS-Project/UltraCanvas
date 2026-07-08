// sample.go — Go syntax-highlighting sample for the UltraCanvas demo.
package main

import (
	"fmt"
	"math"
	"sort"
)

type Shape interface {
	Area() float64
	Name() string
}

type Circle struct {
	Radius float64
}

func (c Circle) Area() float64 { return math.Pi * c.Radius * c.Radius }
func (c Circle) Name() string  { return "circle" }

func main() {
	shapes := []Shape{
		Circle{Radius: 2.5},
		Circle{Radius: 1.0},
	}

	sort.Slice(shapes, func(i, j int) bool {
		return shapes[i].Area() < shapes[j].Area()
	})

	total := 0.0
	for _, s := range shapes {
		fmt.Printf("%s -> %.4f\n", s.Name(), s.Area())
		total += s.Area()
	}
	fmt.Printf("total = %.4f\n", total)
}
