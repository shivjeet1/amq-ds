#pragma once
/**
 * bloom_filter.h
 * ──────────────────────────────────────────────────────────────────────────────
 * A classic Bloom Filter: a probabilistic AMQ data structure.
 *
 * Trade-offs
 *   + Very low memory: ~1.44 bits/element at 1% FPR
 *   + O(k) insert / lookup (k = number of hash functions)
 *   - NO deletion support
 *   - False positives possible; false negatives impossible
 *
 * Theory
 *   Given n expected elements and desired false-positive rate p:
 *     m = -n * ln(p) / (ln2)^2   (bit array size)
 *     k = (m/n) * ln2             (optimal number of hash functions)
 * ──────────────────────────────────────────────────────────────────────────────
 */

#include <vector>
#include <string>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include "murmurhash3.h"

class BloomFilter {
public:
    /**
     * @param n  Expected number of elements to insert
     * @param fp Desired false-positive probability  (0 < fp < 1)
     */
    BloomFilter(size_t n, double fp = 0.01)
        : n_expected_(n), fp_rate_(fp)
    {
        if (fp <= 0.0 || fp >= 1.0)
            throw std::invalid_argument("fp must be in (0,1)");

        // Optimal bit-array size (m) and hash count (k)
        m_ = static_cast<size_t>(
            std::ceil(-static_cast<double>(n) * std::log(fp) /
                      (std::log(2.0) * std::log(2.0))));
        k_ = static_cast<size_t>(
            std::ceil((static_cast<double>(m_) / n) * std::log(2.0)));
        k_ = std::max(k_, size_t(1));

        bits_.assign(m_, false);
        count_ = 0;
    }

    /** Insert an element (string) into the filter. */
    void insert(const std::string& item) {
        for (size_t i = 0; i < k_; ++i) {
            bits_[hash_index(item, static_cast<uint32_t>(i))] = true;
        }
        ++count_;
    }

    /**
     * Query membership.
     * @return true  → item MIGHT be in the set  (could be a false positive)
     * @return false → item is DEFINITELY NOT in the set
     */
    bool query(const std::string& item) const {
        for (size_t i = 0; i < k_; ++i) {
            if (!bits_[hash_index(item, static_cast<uint32_t>(i))])
                return false;
        }
        return true;
    }

    // ── Accessors ──────────────────────────────────────────────────────────
    size_t bit_count()   const { return m_; }
    size_t byte_count()  const { return (m_ + 7) / 8; }
    size_t hash_count()  const { return k_; }
    size_t item_count()  const { return count_; }

    /** Theoretical false-positive probability at current occupancy. */
    double theoretical_fpr() const {
        if (count_ == 0) return 0.0;
        double fill = static_cast<double>(count_) * k_ / static_cast<double>(m_);
        return std::pow(1.0 - std::exp(-fill), static_cast<double>(k_));
    }

private:
    size_t m_;           // bit-array length
    size_t k_;           // number of hash functions
    size_t count_;       // elements inserted
    size_t n_expected_;
    double fp_rate_;
    std::vector<bool> bits_;

    /** Map item + seed → bit-array index using double-hashing trick. */
    size_t hash_index(const std::string& item, uint32_t seed) const {
        // Two independent hashes → linear combination avoids k full hashes
        uint32_t h1 = MurmurHash3_x86_32(item.data(),
                                           static_cast<int>(item.size()), 0);
        uint32_t h2 = MurmurHash3_x86_32(item.data(),
                                           static_cast<int>(item.size()), 0x9747b28c);
        uint64_t combined = static_cast<uint64_t>(h1) + seed * static_cast<uint64_t>(h2);
        return combined % m_;
    }
};
