// binary-trees (java entry) -- build and check full binary trees: a depth-14
// stretch tree, one depth-13 long-lived tree, and the depth 4..12 batch trees.
// Every node is a fresh heap object; nothing is pooled or reused.
//
// Expected output: 1348958.

public class Main {
    private static final int MAX_DEPTH = 13;
    private static final int MIN_DEPTH = 4;

    private static final class Node {
        final Node left;
        final Node right;

        Node(Node left, Node right) {
            this.left = left;
            this.right = right;
        }
    }

    private static Node makeTree(int depth) {
        if (depth == 0) {
            return new Node(null, null);
        }
        return new Node(makeTree(depth - 1), makeTree(depth - 1));
    }

    private static int checkTree(Node node) {
        if (node.left == null) {
            return 1;
        }
        return 1 + checkTree(node.left) + checkTree(node.right);
    }

    public static void main(String[] args) {
        int stretchDepth = MAX_DEPTH + 1;
        int stretchCheck = checkTree(makeTree(stretchDepth));

        Node longLived = makeTree(MAX_DEPTH);
        int longLivedCheck = checkTree(longLived);

        long total = stretchCheck;
        for (int depth = MIN_DEPTH; depth <= MAX_DEPTH; depth += 2) {
            int iterations = 1 << (MAX_DEPTH - depth + MIN_DEPTH);
            long check = 0;
            for (int i = 0; i < iterations; i++) {
                check += checkTree(makeTree(depth));
            }
            total += check;
        }
        total += longLivedCheck;

        System.out.println(total);
    }
}
