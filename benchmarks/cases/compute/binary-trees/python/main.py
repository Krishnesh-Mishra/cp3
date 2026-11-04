"""binary-trees (python entry) -- build full binary trees of depth 4..13 plus a
depth-14 stretch tree, allocating every node on the heap, and check each tree
recursively. One integer.

Run:   python main.py

Expected output: 1348958 (see the task README).
"""

MAX_DEPTH = 13
MIN_DEPTH = 4


class Node:
    __slots__ = ("left", "right")

    def __init__(self, left, right):
        self.left = left
        self.right = right


def make_tree(depth):
    if depth == 0:
        return Node(None, None)
    return Node(make_tree(depth - 1), make_tree(depth - 1))


def check_tree(node):
    if node.left is None:
        return 1
    return 1 + check_tree(node.left) + check_tree(node.right)


def main():
    stretch_depth = MAX_DEPTH + 1
    stretch_check = check_tree(make_tree(stretch_depth))

    long_lived = make_tree(MAX_DEPTH)
    long_lived_check = check_tree(long_lived)

    total = stretch_check
    for depth in range(MIN_DEPTH, MAX_DEPTH + 1, 2):
        iterations = 1 << (MAX_DEPTH - depth + MIN_DEPTH)
        check = 0
        for _ in range(iterations):
            check += check_tree(make_tree(depth))
        total += check

    total += long_lived_check
    print(total)


if __name__ == "__main__":
    main()
