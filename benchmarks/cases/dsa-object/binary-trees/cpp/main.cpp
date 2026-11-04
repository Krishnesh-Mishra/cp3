// binary-trees (cpp entry) -- build the classic full binary trees with
// MAX_DEPTH=13 / MIN_DEPTH=4 using ordinary heap nodes, then check each tree
// recursively and fold every check into one integer.
//
// Build:  g++ -std=c++20 -O3 -o binary-trees.exe main.cpp
// Run:    binary-trees.exe
//
// Same workload and same output in every language entry: 1348958.

#include <iostream>

static const int kMaxDepth = 13;
static const int kMinDepth = 4;

struct Node {
    Node* left;
    Node* right;

    Node(Node* l, Node* r) : left(l), right(r) {}
    ~Node() {
        delete left;
        delete right;
    }
};

static Node* makeTree(int depth) {
    if (depth == 0) return new Node(nullptr, nullptr);
    return new Node(makeTree(depth - 1), makeTree(depth - 1));
}

static long long checkTree(const Node* node) {
    if (node->left == nullptr) return 1;
    return 1 + checkTree(node->left) + checkTree(node->right);
}

int main() {
    const int stretchDepth = kMaxDepth + 1;

    Node* stretch = makeTree(stretchDepth);
    long long total = checkTree(stretch);
    delete stretch;

    Node* longLived = makeTree(kMaxDepth);
    const long long longLivedCheck = checkTree(longLived);

    for (int depth = kMinDepth; depth <= kMaxDepth; depth += 2) {
        const long long iterations = 1LL << (kMaxDepth - depth + kMinDepth);
        long long check = 0;
        for (long long i = 0; i < iterations; ++i) {
            Node* tree = makeTree(depth);
            check += checkTree(tree);
            delete tree;
        }
        total += check;
    }

    total += longLivedCheck;
    delete longLived;

    std::cout << total << "\n";
    return 0;
}
