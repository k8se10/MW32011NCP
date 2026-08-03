import numpy as np
import sys

a = np.fromfile("xenia_mark_001.snap", dtype=np.uint8)
b = np.fromfile("xenia_mark_002.snap", dtype=np.uint8)
print("Loaded", len(a), len(b))

# 1) Single byte: exactly 2 -> exactly 5
mask1 = (a == 2) & (b == 5)
idx1 = np.where(mask1)[0]
print(f"Single-byte 0x02 -> 0x05 candidates: {len(idx1)}")

# 2) 4-byte big-endian uint32: value 2 -> value 5 means bytes [00,00,00,02] -> [00,00,00,05]
# Check all 4-byte-aligned AND unaligned windows where a[i:i+4] == [0,0,0,2] and b[i:i+4] == [0,0,0,5]
target_a = np.array([0, 0, 0, 2], dtype=np.uint8)
target_b = np.array([0, 0, 0, 5], dtype=np.uint8)

# Sliding window match using stride tricks for speed
def find_pattern(arr, pattern):
    n = len(pattern)
    # find positions where arr[i]==pattern[0]
    candidates = np.where(arr == pattern[0])[0]
    candidates = candidates[candidates + n <= len(arr)]
    if len(candidates) == 0:
        return np.array([], dtype=np.int64)
    ok = np.ones(len(candidates), dtype=bool)
    for k in range(1, n):
        ok &= (arr[candidates + k] == pattern[k])
    return candidates[ok]

posA = find_pattern(a, target_a)
print(f"Positions in A matching 00 00 00 02: {len(posA)}")
# Now check which of those positions in B match 00 00 00 05
if len(posA) > 0:
    okB = np.zeros(len(posA), dtype=bool)
    for k in range(4):
        okB_k = (b[posA + k] == target_b[k])
        if k == 0:
            okB = okB_k
        else:
            okB &= okB_k
    idx4 = posA[okB]
    print(f"4-byte BE uint32 0x00000002 -> 0x00000005 candidates: {len(idx4)}")
else:
    idx4 = np.array([], dtype=np.int64)

print("\n--- Single-byte candidates (first 100) ---")
for i in idx1[:100]:
    print(f"0x{i:08X}")

print("\n--- 4-byte BE candidates (first 100) ---")
for i in idx4[:100]:
    print(f"0x{i:08X}")

np.save("candidates_1byte.npy", idx1)
np.save("candidates_4byte.npy", idx4)
