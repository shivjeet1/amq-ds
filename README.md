# amq_ds_benchmark

bloom filter vs cuckoo filter vs just using a hashset like a normal person.

turns out the probabilistic ones use way less memory. who knew. (everyone knew.)

## what's in here

- `bloom_filter.h` — the classic. no deletions. don't ask.
- `cuckoo_filter.h` — like bloom but you can delete stuff. does a little eviction dance when full.
- `murmurhash3.h` — hashing. copied from the internet (it's public domain, relax).
- `main.cpp` — shoves 1M strings through all three and prints who won.
- `CMakeLists.txt` — cmake because makefiles are a crime.

## build & run

```bash
chmod +x run_benchmark.sh
./run_benchmark.sh
```

## Manual build
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel $(nproc)
```

## Run with real dictionary words
```bash
./build/amq_benchmark /usr/share/dict/words
```
that's it. it'll find your dict file if you have one, otherwise generates random strings.

if you don't have cmake: `sudo pacman -S cmake`

## numbers (on my machine, roughly)

| thing | insert | query | false positives | memory |
|---|---|---|---|---|
| bloom filter | ~10M ops/s | ~13M ops/s | ~1% | 585 KB |
| cuckoo filter | ~23M ops/s | ~32M ops/s | ~3% | 1 MB |
| unordered_set | ~1.2M ops/s | ~7.5M ops/s | 0% | 42 MB |

cuckoo is faster. bloom uses less memory. hashset is exact but eats ram.

## why does this exist

daa assignment. the point was to show that you don't always need to store the actual data to answer "is this thing in the set." you just need a fingerprint and a willingness to be occasionally wrong.

false positives = fine. false negatives = never happens.

used in rocksdb, cassandra, chrome safe browsing, etc. so it's not totally useless.

## requirements

- gcc 11+ or clang 13+ (needs c++17)
- cmake 3.16+
- optionally `/usr/share/dict/words` for real words (`sudo pacman -S words`)
