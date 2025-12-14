import sys
import statistics

data_path = sys.argv[1]

collection_size = 0
universe_size = 0
different_tokens = set()
set_size_max = 0
set_size_sum = 0
set_sizes = []

with open(data_path) as f:
    for line in f.readlines():
        tokens = [int(x) for x in line.strip().split(" ")]

        collection_size += 1
        universe_size = max(universe_size, max(tokens))
        different_tokens.update(tokens)
        set_size_max = max(set_size_max, len(tokens))
        set_size_sum += len(tokens)
        set_sizes.append(len(tokens))

print(f"collection_size: {collection_size}")
print(f"universe_size: {universe_size}")
print(f"different_tokens: {len(different_tokens)}")
print(f"set_size_avg: {set_size_sum / collection_size}")
print(f"set_size_max: {set_size_max}")
print(f"set_size_stddev: {statistics.stdev(set_sizes)}")
