import random

n = 10**7
filename = "numbers.txt"

with open(filename, "w") as f:
    for _ in range(n):
        hi = random.getrandbits(64)
        lo = random.getrandbits(64)
        f.write(f"{hi:016x}{lo:016x}\n")
