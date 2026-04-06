#pragma once
/**
 * cuckoo_filter.h
 * ──────────────────────────────────────────────────────────────────────────────
 * A Cuckoo Filter: an AMQ data structure that supports deletion.
 *
 * Key differences from a Bloom Filter
 *   + Supports deletion (unlike Bloom)
 *   + Better cache locality (bucket-based, not bit-scattered)
 *   + O(1) amortized insert / lookup / delete
 *   - Slightly higher per-element overhead
 *   - Insert can fail when the table is very full (load > ~95%)
 *
 * Algorithm (Fan et al., 2014 — "Cuckoo Filter: Practically Better Than Bloom")
 *   1. Compute fingerprint f = fingerprint(item)  [8 or 16 bits]
 *   2. h1 = hash(item) % num_buckets
 *   3. h2 = h1 XOR hash(f) % num_buckets          ← "partial-key cuckoo hashing"
 *      This lets us recover h1 from h2 (and vice-versa) without storing the key.
 *   4. If either bucket has an empty slot, store f there.
 *   5. Otherwise, "evict" a fingerprint from one bucket and place it in its
 *      alternate location. Repeat up to MAX_KICKS times.
 *   6. If eviction cycle exceeds MAX_KICKS, insertion fails (table too full).
 *
 * Memory layout
 *   A bucket holds BUCKET_SIZE fingerprints (default 4).
 *   Fingerprint width = 8 bits → load capacity ≈ 95% of (num_buckets × BUCKET_SIZE)
 * ──────────────────────────────────────────────────────────────────────────────
 */

#include <vector>
#include <string>
#include <cstdint>
#include <cmath>
#include <random>
#include <stdexcept>
#include "murmurhash3.h"

static constexpr size_t  CF_BUCKET_SIZE = 4;     // entries per bucket
static constexpr size_t  CF_MAX_KICKS   = 500;   // eviction retry limit
using Fingerprint = uint8_t;                      // 8-bit fingerprint

class CuckooFilter {
public:
    /**
     * @param capacity  Maximum number of elements (approximate — actual
     *                  capacity is rounded up to a power-of-two buckets).
     */
    explicit CuckooFilter(size_t capacity)
        : count_(0), rng_(std::random_device{}())
    {
        // Each bucket holds CF_BUCKET_SIZE fingerprints.
        // We want load factor ≤ ~85% at requested capacity.
        size_t num_buckets = next_power_of_two(
            static_cast<size_t>(std::ceil(static_cast<double>(capacity) /
                                          (CF_BUCKET_SIZE * 0.85))));
        num_buckets = std::max(num_buckets, size_t(1));
        buckets_.assign(num_buckets, Bucket{});
        num_buckets_ = num_buckets;
        bucket_mask_ = num_buckets - 1; // for fast modulo (power-of-two)
    }

    /**
     * Insert item.
     * @return true on success; false if the table is too full (very rare).
     */
    bool insert(const std::string& item) {
        Fingerprint fp = fingerprint(item);
        size_t i1 = index1(item);
        size_t i2 = alt_index(i1, fp);

        if (buckets_[i1].insert(fp) || buckets_[i2].insert(fp)) {
            ++count_;
            return true;
        }

        // Both buckets full → start eviction
        size_t i = (rng_() & 1) ? i1 : i2; // random starting bucket
        for (size_t n = 0; n < CF_MAX_KICKS; ++n) {
            size_t slot = rng_() % CF_BUCKET_SIZE;
            Fingerprint evicted = buckets_[i].entries[slot];
            buckets_[i].entries[slot] = fp;   // place current fp
            fp = evicted;                      // now need to re-home evicted
            i  = alt_index(i, fp);
            if (buckets_[i].insert(fp)) {
                ++count_;
                return true;
            }
        }
        // Insert failed — filter is over-full
        return false;
    }

    /**
     * Query membership.
     * @return true  → item MIGHT be in the set
     * @return false → item is DEFINITELY NOT in the set
     */
    bool query(const std::string& item) const {
        Fingerprint fp = fingerprint(item);
        size_t i1 = index1(item);
        size_t i2 = alt_index(i1, fp);
        return buckets_[i1].contains(fp) || buckets_[i2].contains(fp);
    }

    /**
     * Delete item from filter (requires the item was previously inserted).
     * Deleting an item that was never inserted may corrupt the filter!
     * @return true if a matching fingerprint was found and removed.
     */
    bool remove(const std::string& item) {
        Fingerprint fp = fingerprint(item);
        size_t i1 = index1(item);
        size_t i2 = alt_index(i1, fp);
        if (buckets_[i1].remove(fp)) { --count_; return true; }
        if (buckets_[i2].remove(fp)) { --count_; return true; }
        return false;
    }

    // ── Accessors ──────────────────────────────────────────────────────────
    size_t size()        const { return count_; }
    size_t num_buckets() const { return num_buckets_; }

    /** Total memory used by the bucket array, in bytes. */
    size_t byte_count() const {
        return num_buckets_ * CF_BUCKET_SIZE * sizeof(Fingerprint);
    }

    /** Load factor as a fraction [0,1]. */
    double load_factor() const {
        return static_cast<double>(count_) /
               (static_cast<double>(num_buckets_) * CF_BUCKET_SIZE);
    }

private:
    // ── Inner bucket ───────────────────────────────────────────────────────
    struct Bucket {
        Fingerprint entries[CF_BUCKET_SIZE] = {0};

        bool insert(Fingerprint fp) {
            for (auto& e : entries) {
                if (e == 0) { e = fp; return true; }
            }
            return false;
        }
        bool contains(Fingerprint fp) const {
            for (auto e : entries) if (e == fp) return true;
            return false;
        }
        bool remove(Fingerprint fp) {
            for (auto& e : entries) {
                if (e == fp) { e = 0; return true; }
            }
            return false;
        }
    };

    // ── Hashing helpers ────────────────────────────────────────────────────
    Fingerprint fingerprint(const std::string& item) const {
        uint32_t h = MurmurHash3_x86_32(item.data(),
                                          static_cast<int>(item.size()), 0xdeadbeef);
        // Avoid fingerprint == 0 (reserved for "empty slot")
        return static_cast<Fingerprint>((h & 0xFF) | 1);
    }

    size_t index1(const std::string& item) const {
        uint32_t h = MurmurHash3_x86_32(item.data(),
                                          static_cast<int>(item.size()), 0);
        return h & bucket_mask_;
    }

    // Partial-key cuckoo hashing: alt index derived from fingerprint only.
    size_t alt_index(size_t index, Fingerprint fp) const {
        uint32_t fp_hash = MurmurHash3_x86_32(&fp, sizeof(fp), 0);
        return (index ^ fp_hash) & bucket_mask_;
    }

    // ── Utilities ──────────────────────────────────────────────────────────
    static size_t next_power_of_two(size_t n) {
        if (n == 0) return 1;
        --n;
        n |= n >> 1; n |= n >> 2; n |= n >> 4;
        n |= n >> 8; n |= n >> 16; n |= n >> 32;
        return n + 1;
    }

    std::vector<Bucket>              buckets_;
    size_t                           num_buckets_;
    size_t                           bucket_mask_;
    size_t                           count_;
    mutable std::mt19937             rng_;
};
