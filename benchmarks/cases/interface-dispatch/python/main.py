"""interface-dispatch (python entry) -- fill a list with 2,000,000 shapes of
three concrete classes, sum every area() through the shared call site, then
repeat the hot loop on a single class and print both sums on one line.

Run:   python main.py

Same workload and same output in every language entry (expected line in the
task README). CPython attribute lookup and bound-method calls are the dispatch
cost; __slots__ keeps the instance side cheap so the numbers stay comparable.
"""

COUNT = 2_000_000


class Circle:
    __slots__ = ("radius",)

    def __init__(self, radius):
        self.radius = radius

    def area(self):
        return 3 * self.radius * self.radius


class Square:
    __slots__ = ("side",)

    def __init__(self, side):
        self.side = side

    def area(self):
        return self.side * self.side


class Rect:
    __slots__ = ("width", "height")

    def __init__(self, width, height):
        self.width = width
        self.height = height

    def area(self):
        return self.width * self.height


def main():
    shapes = []
    for i in range(COUNT):
        kind = i % 3
        if kind == 0:
            shapes.append(Circle(i % 5 + 1))
        elif kind == 1:
            shapes.append(Square(i % 4 + 1))
        else:
            shapes.append(Rect(i % 3 + 1, i % 6 + 2))

    total_dispatch = 0
    for shape in shapes:
        total_dispatch += shape.area()

    circles = [Circle(i % 5 + 1) for i in range(COUNT)]
    total_control = 0
    for circle in circles:
        total_control += circle.area()

    print(total_dispatch, total_control)


if __name__ == "__main__":
    main()
