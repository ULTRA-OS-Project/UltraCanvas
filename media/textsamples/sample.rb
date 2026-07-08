# sample.rb — Ruby syntax-highlighting sample for the UltraCanvas demo.
module Shapes
  class Shape
    attr_reader :name

    def initialize(name)
      @name = name
    end

    def area
      raise NotImplementedError, "#{self.class} must implement #area"
    end
  end

  class Circle < Shape
    def initialize(radius)
      super("circle")
      @radius = radius
    end

    def area
      Math::PI * @radius**2
    end
  end
end

shapes = [Shapes::Circle.new(1.0), Shapes::Circle.new(2.5)]

total = shapes.sum(&:area)
shapes.each do |shape|
  puts format("%<name>s -> %<area>.4f", name: shape.name, area: shape.area)
end

puts "total = #{total.round(4)}"
