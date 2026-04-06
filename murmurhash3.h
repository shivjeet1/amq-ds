#pragma once
#include <cstdint>
#include <cstring>

// MurmurHash3 - fast, high-quality hash function by Austin Appleby
// Public domain

inline uint32_t rotl32(uint32_t x, int8_t r) {
    return (x << r) | (x >> (32 - r));
}

inline uint32_t fmix32(uint32_t h) {
    h ^= h >> 16;
    h *= 0x85ebca6b;
    h ^= h >> 13;
    h *= 0xc2b2ae35;
    h ^= h >> 16;
    return h;
}

inline uint32_t MurmurHash3_x86_32(const void* key, int len, uint32_t seed = 0) {
    const uint8_t* data = (const uint8_t*)key;
    const int nblocks = len / 4;

    uint32_t h1 = seed;
    const uint32_t c1 = 0xcc9e2d51;
    const uint32_t c2 = 0x1b873593;

    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 4);
    for (int i = -nblocks; i; i++) {
        uint32_t k1;
        memcpy(&k1, blocks + i, sizeof(k1));
        k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2;
        h1 ^= k1; h1 = rotl32(h1, 13); h1 = h1 * 5 + 0xe6546b64;
    }

    const uint8_t* tail = (const uint8_t*)(data + nblocks * 4);
    uint32_t k1 = 0;
    switch (len & 3) {
        case 3: k1 ^= tail[2] << 16; [[fallthrough]];
        case 2: k1 ^= tail[1] << 8;  [[fallthrough]];
        case 1: k1 ^= tail[0]; k1 *= c1; k1 = rotl32(k1, 15); k1 *= c2; h1 ^= k1;
    }

    h1 ^= len;
    return fmix32(h1);
}

// Produce two independent 32-bit hashes from one call
inline void MurmurHash3_x86_128(const void* key, int len,
                                  uint32_t seed, uint32_t out[4]) {
    const uint8_t* data = (const uint8_t*)key;
    const int nblocks = len / 16;
    uint32_t h1 = seed, h2 = seed, h3 = seed, h4 = seed;
    const uint32_t c1 = 0x239b961b, c2 = 0xab0e9789;
    const uint32_t c3 = 0x38b34ae5, c4 = 0xa1e38b93;
    const uint32_t* blocks = (const uint32_t*)(data + nblocks * 16);
    for (int i = -nblocks; i; i++) {
        uint32_t k1, k2, k3, k4;
        memcpy(&k1, blocks + i*4 + 0, 4);
        memcpy(&k2, blocks + i*4 + 1, 4);
        memcpy(&k3, blocks + i*4 + 2, 4);
        memcpy(&k4, blocks + i*4 + 3, 4);
        k1 *= c1; k1 = rotl32(k1,15); k1 *= c2; h1 ^= k1;
        h1 = rotl32(h1,19); h1 += h2; h1 = h1*5+0x561ccd1b;
        k2 *= c2; k2 = rotl32(k2,16); k2 *= c3; h2 ^= k2;
        h2 = rotl32(h2,17); h2 += h3; h2 = h2*5+0x0bcaa747;
        k3 *= c3; k3 = rotl32(k3,17); k3 *= c4; h3 ^= k3;
        h3 = rotl32(h3,15); h3 += h4; h3 = h3*5+0x96cd1c35;
        k4 *= c4; k4 = rotl32(k4,18); k4 *= c1; h4 ^= k4;
        h4 = rotl32(h4,13); h4 += h1; h4 = h4*5+0x32ac3b17;
    }
    h1 ^= len; h2 ^= len; h3 ^= len; h4 ^= len;
    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;
    h1 = fmix32(h1); h2 = fmix32(h2); h3 = fmix32(h3); h4 = fmix32(h4);
    h1 += h2; h1 += h3; h1 += h4;
    h2 += h1; h3 += h1; h4 += h1;
    out[0] = h1; out[1] = h2; out[2] = h3; out[3] = h4;
}
