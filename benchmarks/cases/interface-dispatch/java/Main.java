// interface-dispatch (java entry) -- fill an array with 2,000,000 shapes of
// three concrete types behind an interface, sum every area() through the
// interface, then repeat the hot loop as direct calls on a concrete type and
// print both sums on one line.
//
// Same workload and same output as the C++, Rust, Go, JavaScript and Python
// entries. The interface call site is polymorphic over three receivers; the
// control loop is monomorphic and can be inlined.

public class Main {
    private static final int COUNT = 2_000_000;

    private interface Shape {
        long area();
    }

    private static final class Circle implements Shape {
        final long radius;

        Circle(long radius) {
            this.radius = radius;
        }

        @Override
        public long area() {
            return 3 * radius * radius;
        }
    }

    private static final class Square implements Shape {
        final long side;

        Square(long side) {
            this.side = side;
        }

        @Override
        public long area() {
            return side * side;
        }
    }

    private static final class Rect implements Shape {
        final long width;
        final long height;

        Rect(long width, long height) {
            this.width = width;
            this.height = height;
        }

        @Override
        public long area() {
            return width * height;
        }
    }

    public static void main(String[] args) {
        Shape[] shapes = new Shape[COUNT];
        for (int i = 0; i < COUNT; i++) {
            int kind = i % 3;
            if (kind == 0) {
                shapes[i] = new Circle(i % 5 + 1);
            } else if (kind == 1) {
                shapes[i] = new Square(i % 4 + 1);
            } else {
                shapes[i] = new Rect(i % 3 + 1, i % 6 + 2);
            }
        }

        long totalDispatch = 0;
        for (Shape shape : shapes) {
            totalDispatch = totalDispatch + shape.area();
        }

        Circle[] circles = new Circle[COUNT];
        for (int i = 0; i < COUNT; i++) {
            circles[i] = new Circle(i % 5 + 1);
        }

        long totalControl = 0;
        for (Circle circle : circles) {
            totalControl = totalControl + circle.area();
        }

        System.out.println(totalDispatch + " " + totalControl);
    }
}
