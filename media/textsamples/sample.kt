// sample.kt — Kotlin syntax-highlighting sample for the UltraCanvas demo.
package com.ultracanvas.demo

import kotlin.math.PI

interface Shape {
    val name: String
    fun area(): Double
}

class Circle(private val radius: Double) : Shape {
    override val name: String get() = "circle"
    override fun area(): Double = PI * radius * radius
}

fun totalArea(shapes: List<Shape>): Double = shapes.sumOf { it.area() }

fun main() {
    val shapes = listOf(Circle(1.0), Circle(2.5))

    shapes.forEach { shape ->
        println("%s -> %.4f".format(shape.name, shape.area()))
    }

    println("total = ${"%.4f".format(totalArea(shapes))}")
}
