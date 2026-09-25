// sample.scala — Scala syntax-highlighting sample for the UltraCanvas demo.
package demo.shapes

import scala.math.{Pi, sqrt}

sealed trait Shape {
  def area: Double
}

final case class Circle(radius: Double) extends Shape {
  override def area: Double = Pi * radius * radius
}

final case class Rect(width: Double, height: Double) extends Shape {
  override def area: Double = width * height
  def diagonal: Double = sqrt(width * width + height * height)
}

object ShapeReport {
  /* Largest first, ties broken by name. */
  def describe(shapes: Seq[Shape]): List[String] =
    shapes.sortBy(-_.area).toList.map {
      case Circle(r)  => s"circle r=$r area=${"%.2f".format(Pi * r * r)}"
      case r: Rect    => f"rect ${r.width}%.1f x ${r.height}%.1f diag=${r.diagonal}%.2f"
    }

  def main(args: Array[String]): Unit = {
    val shapes = Vector(Circle(1.5), Rect(3, 4), Circle(0.5))
    val total = shapes.foldLeft(0.0)(_ + _.area)
    describe(shapes).foreach(println)
    println(s"total area: $total")
  }
}
