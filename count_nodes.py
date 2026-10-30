total = 0
for d in [4, 6, 8, 10, 12]:
    iters = 1 << (13 - d + 4)   # 1<<(MAX_DEPTH - depth + MIN_DEPTH)
    nodes = 2**(d+1) - 1        # full binary tree nodes
    total += iters * nodes
print('nodes built per run:', total)
print('makeRef calls (1 per node):', total)
print('makeArray calls (1 per node):', total)
