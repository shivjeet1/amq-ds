/**
 * main.cpp — Probabilistic Membership Filter Benchmark
 * ═══════════════════════════════════════════════════════════════════════════════
 * Benchmarks three AMQ / membership structures:
 *   1. Bloom Filter      (no deletion, bitset-based)
 *   2. Cuckoo Filter     (supports deletion, bucket-based)
 *   3. std::unordered_set (exact, high memory)
 *
 * Metrics captured
 *   • Insert throughput  (ops/sec)
 *   • Lookup throughput  (ops/sec)
 *   • False-positive rate
 *   • Memory usage (bytes)
 *
 * Usage
 *   ./amq_benchmark                 # random synthetic strings
 *   ./amq_benchmark /usr/share/dict/words   # real dictionary words
 * ═══════════════════════════════════════════════════════════════════════════════
 */

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <unordered_set>
#include <random>
#include <chrono>
#include <iomanip>
#include <algorithm>
#include <cstdlib>

#include "bloom_filter.h"
#include "cuckoo_filter.h"

// ── Configuration ─────────────────────────────────────────────────────────────
static constexpr size_t TOTAL_STRINGS   = 1'000'000;
static constexpr size_t INSERT_COUNT    = 500'000;
static constexpr size_t QUERY_COUNT     = 500'000;  // the other half (non-members)
static constexpr double BLOOM_TARGET_FP = 0.01;     // 1% false-positive rate

// ── Timing helpers ────────────────────────────────────────────────────────────
using Clock     = std::chrono::high_resolution_clock;
using TimePoint = Clock::time_point;

static double elapsed_ms(TimePoint start, TimePoint end) {
    return std::chrono::duration<double, std::milli>(end - start).count();
}
static double ops_per_sec(size_t n, double ms) {
    return (ms > 0) ? static_cast<double>(n) / (ms / 1000.0) : 0;
}

// ── String generation ─────────────────────────────────────────────────────────
static std::vector<std::string> generate_random_strings(size_t n) {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<int> len_dist(6, 20);
    std::uniform_int_distribution<int> chr_dist('a', 'z');

    std::vector<std::string> out;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        int len = len_dist(rng);
        std::string s(len, ' ');
        for (auto& c : s) c = static_cast<char>(chr_dist(rng));
        out.push_back(std::move(s));
    }
    return out;
}

static std::vector<std::string> load_from_file(const std::string& path) {
    std::vector<std::string> words;
    std::ifstream f(path);
    if (!f) {
        std::cerr << "[WARN] Cannot open " << path << " — falling back to random strings\n";
        return {};
    }
    std::string w;
    while (std::getline(f, w)) {
        if (!w.empty()) words.push_back(w);
    }
    std::cout << "[INFO] Loaded " << words.size() << " words from " << path << "\n";
    return words;
}

// ── Pretty printers ───────────────────────────────────────────────────────────
static void print_separator(char c = '─', int width = 72) {
    std::cout << std::string(width, c) << "\n";
}
static void print_header(const std::string& title) {
    print_separator('═');
    std::cout << "  " << title << "\n";
    print_separator('═');
}

static void print_row(const std::string& label, const std::string& value) {
    std::cout << std::left << std::setw(30) << ("  " + label)
              << value << "\n";
}

struct BenchResult {
    std::string name;
    double  insert_ms;
    double  query_ms;
    size_t  false_positives;
    size_t  query_total;
    size_t  memory_bytes;
    size_t  items_inserted;
};

static void print_result(const BenchResult& r) {
    double fpr = static_cast<double>(r.false_positives) /
                 static_cast<double>(r.query_total) * 100.0;
    double ins_ops = ops_per_sec(r.items_inserted, r.insert_ms);
    double qry_ops = ops_per_sec(r.query_total,   r.query_ms);

    print_separator();
    std::cout << "  ▶  " << r.name << "\n";
    print_separator();
    print_row("Items inserted:",
              std::to_string(r.items_inserted));
    print_row("Items queried (non-members):",
              std::to_string(r.query_total));

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << r.insert_ms << " ms"
        << "  (" << std::setprecision(0) << ins_ops << " ops/sec)";
    print_row("Insert time:", oss.str());

    oss.str(""); oss.clear();
    oss << std::fixed << std::setprecision(2) << r.query_ms << " ms"
        << "  (" << std::setprecision(0) << qry_ops << " ops/sec)";
    print_row("Query time:", oss.str());

    oss.str(""); oss.clear();
    oss << r.false_positives << "  (" << std::setprecision(4) << fpr << "%)";
    print_row("False positives:", oss.str());

    oss.str(""); oss.clear();
    if (r.memory_bytes < 1024)
        oss << r.memory_bytes << " bytes";
    else if (r.memory_bytes < 1024*1024)
        oss << std::setprecision(2) << r.memory_bytes/1024.0 << " KB";
    else
        oss << std::setprecision(2) << r.memory_bytes/(1024.0*1024.0) << " MB";
    print_row("Memory (approximate):", oss.str());
}

// ── Benchmark: Bloom Filter ───────────────────────────────────────────────────
static BenchResult bench_bloom(const std::vector<std::string>& insert_set,
                                const std::vector<std::string>& query_set) {
    BloomFilter bf(insert_set.size(), BLOOM_TARGET_FP);

    auto t0 = Clock::now();
    for (auto& s : insert_set) bf.insert(s);
    auto t1 = Clock::now();

    size_t fp = 0;
    for (auto& s : query_set) if (bf.query(s)) ++fp;
    auto t2 = Clock::now();

    return { "Bloom Filter",
             elapsed_ms(t0, t1), elapsed_ms(t1, t2),
             fp, query_set.size(),
             bf.byte_count(),
             insert_set.size() };
}

// ── Benchmark: Cuckoo Filter ──────────────────────────────────────────────────
static BenchResult bench_cuckoo(const std::vector<std::string>& insert_set,
                                 const std::vector<std::string>& query_set) {
    CuckooFilter cf(insert_set.size());

    size_t failed = 0;
    auto t0 = Clock::now();
    for (auto& s : insert_set) if (!cf.insert(s)) ++failed;
    auto t1 = Clock::now();

    size_t fp = 0;
    for (auto& s : query_set) if (cf.query(s)) ++fp;
    auto t2 = Clock::now();

    if (failed > 0)
        std::cerr << "[WARN] Cuckoo: " << failed << " inserts failed (table too full)\n";

    // Demonstrate deletion
    size_t del_count = std::min(size_t(10), insert_set.size());
    for (size_t i = 0; i < del_count; ++i) cf.remove(insert_set[i]);

    return { "Cuckoo Filter",
             elapsed_ms(t0, t1), elapsed_ms(t1, t2),
             fp, query_set.size(),
             cf.byte_count(),
             insert_set.size() - failed };
}

// ── Benchmark: std::unordered_set ────────────────────────────────────────────
static BenchResult bench_hashset(const std::vector<std::string>& insert_set,
                                  const std::vector<std::string>& query_set) {
    std::unordered_set<std::string> hs;
    hs.reserve(insert_set.size());

    auto t0 = Clock::now();
    for (auto& s : insert_set) hs.insert(s);
    auto t1 = Clock::now();

    size_t found = 0;
    for (auto& s : query_set) if (hs.count(s)) ++found;
    auto t2 = Clock::now();

    // Approximate memory: each string ≈ 32 bytes on-heap + ~56-byte node
    size_t mem = hs.size() * (32 + 56);

    return { "std::unordered_set",
             elapsed_ms(t0, t1), elapsed_ms(t1, t2),
             found,              // these are true non-members → 0 FP expected
             query_set.size(),
             mem,
             insert_set.size() };
}

// ── Comparison summary table ──────────────────────────────────────────────────
static void print_comparison(const std::vector<BenchResult>& results) {
    print_header("COMPARISON SUMMARY");
    std::cout << std::left
              << std::setw(22) << "Structure"
              << std::setw(14) << "Insert(ops/s)"
              << std::setw(14) << "Query(ops/s)"
              << std::setw(12) << "FP Rate"
              << std::setw(10) << "Memory"
              << "\n";
    print_separator('-');
    for (auto& r : results) {
        double fpr = static_cast<double>(r.false_positives) /
                     static_cast<double>(r.query_total) * 100.0;
        double ins_ops = ops_per_sec(r.items_inserted, r.insert_ms);
        double qry_ops = ops_per_sec(r.query_total,   r.query_ms);

        std::ostringstream mem_str;
        if (r.memory_bytes < 1024)       mem_str << r.memory_bytes << "B";
        else if (r.memory_bytes < 1<<20) mem_str << std::setprecision(1) << r.memory_bytes/1024.0 << "KB";
        else                             mem_str << std::setprecision(1) << r.memory_bytes/(1024.0*1024.0) << "MB";

        std::ostringstream fpr_str;
        fpr_str << std::fixed << std::setprecision(3) << fpr << "%";

        std::cout << std::left
                  << std::setw(22) << r.name
                  << std::setw(14) << static_cast<size_t>(ins_ops)
                  << std::setw(14) << static_cast<size_t>(qry_ops)
                  << std::setw(12) << fpr_str.str()
                  << std::setw(10) << mem_str.str()
                  << "\n";
    }
    print_separator('═');
}

// ── Entry point ───────────────────────────────────────────────────────────────
int main(int argc, char* argv[]) {
    print_header("AMQ BENCHMARK — Bloom vs Cuckoo vs HashSet");

    // 1. Prepare strings
    std::vector<std::string> all_strings;
    if (argc > 1) {
        all_strings = load_from_file(argv[1]);
    }

    if (all_strings.size() < TOTAL_STRINGS) {
        if (!all_strings.empty())
            std::cout << "[INFO] Dictionary too small; padding with random strings.\n";
        auto extra = generate_random_strings(TOTAL_STRINGS - all_strings.size());
        all_strings.insert(all_strings.end(), extra.begin(), extra.end());
    }

    // Shuffle for unbiased split
    std::mt19937 rng(12345);
    std::shuffle(all_strings.begin(), all_strings.end(), rng);

    std::vector<std::string> insert_set(all_strings.begin(),
                                         all_strings.begin() + INSERT_COUNT);
    std::vector<std::string> query_set (all_strings.begin() + INSERT_COUNT,
                                         all_strings.begin() + INSERT_COUNT + QUERY_COUNT);

    std::cout << "\n[INFO] Insert set : " << insert_set.size() << " items\n";
    std::cout << "[INFO] Query set  : " << query_set.size()  << " items (non-members)\n\n";

    // 2. Run benchmarks
    std::vector<BenchResult> results;
    results.push_back(bench_bloom (insert_set, query_set));
    results.push_back(bench_cuckoo(insert_set, query_set));
    results.push_back(bench_hashset(insert_set, query_set));

    // 3. Print individual results
    for (auto& r : results) print_result(r);

    // 4. Summary table
    print_comparison(results);

    // 5. DAA Analysis notes
    std::cout << "\n  DAA ANALYSIS NOTES\n";
    print_separator();
    std::cout << "  Space Trade-off  : Bloom/Cuckoo trade exact membership for ~10-20x\n"
              << "                     memory reduction vs unordered_set.\n"
              << "  Time Complexity  : Bloom O(k), Cuckoo O(1) amortized.\n"
              << "  Deletion         : Only Cuckoo supports safe deletion.\n"
              << "  Use-case         : Pre-filter for disk/network lookups (e.g., RocksDB)\n"
              << "                     where a false positive costs one extra I/O read,\n"
              << "                     but a false negative is unacceptable.\n";
    print_separator('═');

    return 0;
}
