// interface-dispatch (js entry) -- fill an array with 2,000,000 shapes of
// three concrete classes, sum every area() through the shared call site, then
// repeat the hot loop on a single class and print both sums on one line.
//
// Run: node main.mjs
//
// Same workload and same output in every language entry (expected line in the
// task README). The 3-type call site is polymorphic for the JIT and the
// control loop is monomorphic, so the gap between the two timings is the
// inline-cache cost.

class Circle {
  constructor(radius) {
    this.radius = radius;
  }
  area() {
    return 3 * this.radius * this.radius;
  }
}

class Square {
  constructor(side) {
    this.side = side;
  }
  area() {
    return this.side * this.side;
  }
}

class Rect {
  constructor(width, height) {
    this.width = width;
    this.height = height;
  }
  area() {
    return this.width * this.height;
  }
}

const COUNT = 2_000_000;

const shapes = [];
for (let i = 0; i < COUNT; i++) {
  if (i % 3 === 0) {
    shapes.push(new Circle((i % 5) + 1));
  } else if (i % 3 === 1) {
    shapes.push(new Square((i % 4) + 1));
  } else {
    shapes.push(new Rect((i % 3) + 1, (i % 6) + 2));
  }
}

let totalDispatch = 0;
for (const shape of shapes) {
  totalDispatch += shape.area();
}

const circles = [];
for (let i = 0; i < COUNT; i++) {
  circles.push(new Circle((i % 5) + 1));
}

let totalControl = 0;
for (const circle of circles) {
  totalControl += circle.area();
}

console.log(totalDispatch, totalControl);
