// interface-dispatch (rust entry) -- fill an array with 2,000,000 shapes of
// three concrete types behind a trait object, sum every area() through the
// trait object, then repeat the hot loop as direct calls on a concrete type
// and print both sums on one line.
//
// Build:  rustc -O main.rs -o interface-dispatch.exe
// Run:    interface-dispatch.exe
//
// Same workload and same output in every language entry (expected line in the
// task README). Vec<Box<dyn Shape>> is the dynamic-dispatch shape; the control
// loop over Vec<Circle> gets static dispatch, and the timing gap between the
// two loops is the point of the task.

trait Shape {
    fn area(&self) -> i64;
}

struct Circle {
    radius: i64,
}

impl Shape for Circle {
    fn area(&self) -> i64 {
        3 * self.radius * self.radius
    }
}

struct Square {
    side: i64,
}

impl Shape for Square {
    fn area(&self) -> i64 {
        self.side * self.side
    }
}

struct Rect {
    width: i64,
    height: i64,
}

impl Shape for Rect {
    fn area(&self) -> i64 {
        self.width * self.height
    }
}

fn main() {
    const COUNT: i64 = 2_000_000;

    let mut shapes: Vec<Box<dyn Shape>> = Vec::with_capacity(COUNT as usize);
    for i in 0..COUNT {
        if i % 3 == 0 {
            shapes.push(Box::new(Circle { radius: i % 5 + 1 }));
        } else if i % 3 == 1 {
            shapes.push(Box::new(Square { side: i % 4 + 1 }));
        } else {
            shapes.push(Box::new(Rect { width: i % 3 + 1, height: i % 6 + 2 }));
        }
    }

    let total_dispatch: i64 = shapes.iter().map(|shape| shape.area()).sum();

    let circles: Vec<Circle> = (0..COUNT).map(|i| Circle { radius: i % 5 + 1 }).collect();
    let total_control: i64 = circles.iter().map(|circle| circle.area()).sum();

    println!("{} {}", total_dispatch, total_control);
}
