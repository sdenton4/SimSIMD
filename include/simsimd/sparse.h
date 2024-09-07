/**
 *  @file       sparse.h
 *  @brief      SIMD-accelerated functions for Sparse Vectors.
 *  @author     Ash Vardanian
 *  @date       March 21, 2024
 *
 *  Contains:
 *  - Set Intersection ~ Jaccard Distance
 *
 *  For datatypes:
 *  - u16: for vocabularies under 64 K tokens
 *  - u32: for vocabularies under 4 B tokens
 *
 *  For hardware architectures:
 *  - x86 (AVX512)
 *  - Arm (SVE)
 *
 *  Interestingly, to implement sparse distances and products, the most important function
 *  is analogous to `std::set_intersection`, that outputs the intersection of two sorted
 *  sequences. The naive implementation of that function would look like:
 *
 *      std::size_t intersection = 0;
 *      while (i != a_length && j != b_length) {
 *          scalar_t ai = a[i];
 *          scalar_t bj = b[j];
 *          intersection += ai == bj;
 *          i += ai < bj;
 *          j += ai >= bj;
 *      }
 *
 *  Assuming we are dealing with sparse arrays, most of the time we are just evaluating
 *  branches and skipping entries. So what if we could skip multiple entries at a time
 *  searching for the next chunk, where an intersection is possible.
 *
 *  x86 intrinsics: https://www.intel.com/content/www/us/en/docs/intrinsics-guide/
 *  Arm intrinsics: https://developer.arm.com/architectures/instruction-sets/intrinsics/
 */
#ifndef SIMSIMD_SPARSE_H
#define SIMSIMD_SPARSE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// clang-format off

/*  Implements the serial set intersection algorithm, similar to `std::set_intersection in C++ STL`,
 *  but uses clever galloping logic, if the arrays significantly differ in size.
 */
SIMSIMD_PUBLIC void simsimd_intersect_u16_serial(simsimd_u16_t const* a, simsimd_u16_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);
SIMSIMD_PUBLIC void simsimd_intersect_u32_serial(simsimd_u32_t const* a, simsimd_u32_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);

/*  Implements the most naive set intersection algorithm, similar to `std::set_intersection in C++ STL`,
 *  naively enumerating the elements of two arrays.
 */
SIMSIMD_PUBLIC void simsimd_intersect_u16_accurate(simsimd_u16_t const* a, simsimd_u16_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);
SIMSIMD_PUBLIC void simsimd_intersect_u32_accurate(simsimd_u32_t const* a, simsimd_u32_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);

/*  SIMD-powered backends for Arm SVE, mostly using 32-bit arithmetic over variable-length platform-defined word sizes.
 *  Designed for Arm Graviton 3, Microsoft Cobalt, as well as Nvidia Grace and newer Ampere Altra CPUs.
 */
SIMSIMD_PUBLIC void simsimd_intersect_u32_sve(simsimd_u32_t const* a, simsimd_u32_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);
SIMSIMD_PUBLIC void simsimd_intersect_u16_sve(simsimd_u16_t const* a, simsimd_u16_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);

/*  SIMD-powered backends for various generations of AVX512 CPUs.
 *  Skylake is handy, as it supports masked loads and other operations, avoiding the need for the tail loop.
 */
SIMSIMD_PUBLIC void simsimd_intersect_u32_ice(simsimd_u32_t const* a, simsimd_u32_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);
SIMSIMD_PUBLIC void simsimd_intersect_u16_ice(simsimd_u16_t const* a, simsimd_u16_t const* b, simsimd_size_t a_length, simsimd_size_t b_length, simsimd_distance_t* results);
// clang-format on

#define SIMSIMD_MAKE_INTERSECT_LINEAR(name, input_type, accumulator_type)                                              \
    SIMSIMD_PUBLIC void simsimd_intersect_##input_type##_##name(                                                       \
        simsimd_##input_type##_t const* a, simsimd_##input_type##_t const* b, simsimd_size_t a_length,                 \
        simsimd_size_t b_length, simsimd_distance_t* result) {                                                         \
        simsimd_##accumulator_type##_t intersection = 0;                                                               \
        simsimd_size_t i = 0, j = 0;                                                                                   \
        while (i != a_length && j != b_length) {                                                                       \
            simsimd_##input_type##_t ai = a[i];                                                                        \
            simsimd_##input_type##_t bj = b[j];                                                                        \
            intersection += ai == bj;                                                                                  \
            i += ai < bj;                                                                                              \
            j += ai >= bj;                                                                                             \
        }                                                                                                              \
        *result = intersection;                                                                                        \
    }

SIMSIMD_MAKE_INTERSECT_LINEAR(accurate, u16, size) // simsimd_intersect_u16_accurate
SIMSIMD_MAKE_INTERSECT_LINEAR(accurate, u32, size) // simsimd_intersect_u32_accurate

#define SIMSIMD_MAKE_INTERSECT_GALLOPING(name, input_type, accumulator_type)                                           \
    SIMSIMD_PUBLIC simsimd_size_t simsimd_galloping_search_##input_type(simsimd_##input_type##_t const* array,         \
                                                                        simsimd_size_t start, simsimd_size_t length,   \
                                                                        simsimd_##input_type##_t val) {                \
        simsimd_size_t low = start;                                                                                    \
        simsimd_size_t high = start + 1;                                                                               \
        while (high < length && array[high] < val) {                                                                   \
            low = high;                                                                                                \
            high = (2 * high < length) ? 2 * high : length;                                                            \
        }                                                                                                              \
        while (low < high) {                                                                                           \
            simsimd_size_t mid = low + (high - low) / 2;                                                               \
            if (array[mid] < val) {                                                                                    \
                low = mid + 1;                                                                                         \
            } else {                                                                                                   \
                high = mid;                                                                                            \
            }                                                                                                          \
        }                                                                                                              \
        return low;                                                                                                    \
    }                                                                                                                  \
                                                                                                                       \
    SIMSIMD_PUBLIC void simsimd_intersect_##input_type##_##name(                                                       \
        simsimd_##input_type##_t const* shorter, simsimd_##input_type##_t const* longer,                               \
        simsimd_size_t shorter_length, simsimd_size_t longer_length, simsimd_distance_t* result) {                     \
        /* Swap arrays if necessary, as we want "longer" to be larger than "shorter" */                                \
        if (longer_length < shorter_length) {                                                                          \
            simsimd_##input_type##_t const* temp = shorter;                                                            \
            shorter = longer;                                                                                          \
            longer = temp;                                                                                             \
            simsimd_size_t temp_length = shorter_length;                                                               \
            shorter_length = longer_length;                                                                            \
            longer_length = temp_length;                                                                               \
        }                                                                                                              \
                                                                                                                       \
        /* Use the accurate implementation if galloping is not beneficial */                                           \
        if (longer_length < 64 * shorter_length) {                                                                     \
            simsimd_intersect_##input_type##_accurate(shorter, longer, shorter_length, longer_length, result);         \
            return;                                                                                                    \
        }                                                                                                              \
                                                                                                                       \
        /* Perform galloping, shrinking the target range */                                                            \
        simsimd_##accumulator_type##_t intersection = 0;                                                               \
        simsimd_size_t j = 0;                                                                                          \
        for (simsimd_size_t i = 0; i < shorter_length; ++i) {                                                          \
            simsimd_##input_type##_t shorter_i = shorter[i];                                                           \
            j = simsimd_galloping_search_##input_type(longer, j, longer_length, shorter_i);                            \
            if (j < longer_length && longer[j] == shorter_i) {                                                         \
                intersection++;                                                                                        \
            }                                                                                                          \
        }                                                                                                              \
        *result = intersection;                                                                                        \
    }

SIMSIMD_MAKE_INTERSECT_GALLOPING(serial, u16, size) // simsimd_intersect_u16_serial
SIMSIMD_MAKE_INTERSECT_GALLOPING(serial, u32, size) // simsimd_intersect_u32_serial

#if SIMSIMD_TARGET_X86
#if SIMSIMD_TARGET_ICE
#pragma GCC push_options
#pragma GCC target("avx512f", "avx512vl", "bmi2", "avx512bw")
#pragma clang attribute push(__attribute__((target("avx512f,avx512vl,bmi2,avx512bw"))), apply_to = function)

SIMSIMD_PUBLIC void simsimd_intersect_u16_ice(simsimd_u16_t const* shorter, simsimd_u16_t const* longer,
                                              simsimd_size_t shorter_length, simsimd_size_t longer_length,
                                              simsimd_distance_t* results) {
    simsimd_size_t intersection_count = 0;
    simsimd_size_t shorter_idx = 0, longer_idx = 0;
    simsimd_size_t shorter_load_size, longer_load_size;
    __mmask32 shorter_mask, longer_mask;

    union vec_t {
        __m512i zmm;
        simsimd_u16_t u16[32];
        simsimd_u8_t u8[64];
    } shorter_members, shorter_member, longer_members, broadcast_first, rotate_first;

    // We will be walking through batches of elements from the shorter array,
    // and comparing them with the longer array. The first element of the shorter
    // array will be broadcasted to a vector. As there is no cheap `_mm512_permutexvar_epi16`-like
    // instruction in AVX512, we couldt try using `vpshufb` via `_mm512_shuffle_epi8`,
    // to load the 1st and 2nd byte of the first element into each 16-bit word,
    // but it only works within 128-bit lanes.
    // For comparison:
    //
    //  - `_mm512_permutex_epi64` - needs F - 3 cycles latency
    //  - `_mm512_shuffle_epi8` - needs BW - 1 cycle latency
    //  - `_mm512_permutexvar_epi16` - needs BW - 4-6 cycles latency
    //  - `_mm512_permutexvar_epi8` - needs VBMI - 3 cycles latency
    broadcast_first.zmm = _mm512_set1_epi16(0x0100);

simsimd_intersect_u16_ice_cycle:
    // At any given cycle, make sure we use the shorter array as the first array.
    if ((shorter_length - shorter_idx) > (longer_length - longer_idx)) {
        simsimd_u16_t const* temp_array = shorter;
        shorter = longer, longer = temp_array;
        simsimd_size_t temp_length = shorter_length;
        shorter_length = longer_length, longer_length = temp_length;
        simsimd_size_t temp_idx = shorter_idx;
        shorter_idx = longer_idx, longer_idx = temp_idx;
    }

    // Estimate if we need to load the whole array or just a part of it.
    simsimd_size_t shorter_remaining = shorter_length - shorter_idx;
    if (shorter_remaining < 32) {
        shorter_load_size = shorter_remaining;
        shorter_mask = (__mmask32)_bzhi_u32(0xFFFFFFFF, shorter_remaining);
    } else {
        shorter_load_size = 32;
        shorter_mask = 0xFFFFFFFF;
    }
    shorter_members.zmm = _mm512_maskz_loadu_epi16(shorter_mask, (__m512i const*)(shorter + shorter_idx));

simsimd_intersect_u16_ice_cycle_longer:
    //
    simsimd_size_t longer_remaining = longer_length - longer_idx;
    if (longer_remaining < 32) {
        longer_load_size = longer_remaining;
        longer_mask = (__mmask32)_bzhi_u32(0xFFFFFFFF, longer_remaining);
    } else {
        longer_load_size = 32;
        longer_mask = 0xFFFFFFFF;
    }
    longer_members.zmm = _mm512_maskz_loadu_epi16(longer_mask, (__m512i const*)(longer + longer_idx));
    shorter_member.zmm = _mm512_permutexvar_epi8(broadcast_first.zmm, shorter_members.zmm);

    // Compare `shorter_member` with each element in `longer_members.zmm`,
    // and jump to the position of the match. There can be only one match at most!
    __mmask32 equal_mask = _mm512_mask_cmpeq_epu16_mask(longer_mask, shorter_member.zmm, longer_members.zmm);
    simsimd_size_t equal_count = equal_mask != 0;
    intersection_count += equal_count;

    // When comparing a scalar against a sorted array, we can find three types of elements:
    // - entries that scalar is greater than,
    // - entries that scalar is equal to,
    // - entries that scalar is less than,
    // ... in that order! Any of them can be an empty set.
    __mmask32 greater_mask = _mm512_mask_cmplt_epu16_mask(longer_mask, longer_members.zmm, shorter_member.zmm);
    int greater_count = _mm_popcnt_u32(greater_mask);
    int smaller_exists = longer_load_size > greater_count - equal_count;
    int shorter_will_advance = equal_count | smaller_exists;

    // Advance the first array:
    // - to the next element, if a match was found,
    // - to the next element, if the current element is smaller than any elements in the second array.
    shorter_idx += shorter_will_advance;
    // Advance the second array:
    // - to the next element after match, if a match was found,
    // - to the first element that is greater than the current element in the first array, if no match was found.
    longer_idx += greater_count + equal_count;

    if (shorter_will_advance) {
        if (shorter_load_size) {
            --shorter_load_size;
            // TODO:
            shorter_members.zmm = _mm512_alignr_epi32(shorter_members.zmm, shorter_members.zmm, 1);
            goto simsimd_intersect_u16_ice_cycle_longer;
        }
    } else
        goto simsimd_intersect_u16_ice_cycle_longer;

    if (shorter_idx < shorter_length && longer_idx < longer_length)
        goto simsimd_intersect_u16_ice_cycle;

    *results = intersection_count;
}

SIMSIMD_PUBLIC void simsimd_intersect_u32_ice(simsimd_u32_t const* shorter, simsimd_u32_t const* longer,
                                              simsimd_size_t shorter_length, simsimd_size_t longer_length,
                                              simsimd_distance_t* results) {
    simsimd_size_t intersection_count = 0;
    simsimd_size_t shorter_remaining = shorter_length, longer_remaining = longer_length;
    simsimd_size_t shorter_load_size, longer_load_size;
    __mmask16 shorter_mask, longer_mask;
    __mmask16 equal_mask, greater_mask;

    union vec_t {
        __m512i zmm;
        simsimd_u32_t u32[16];
        simsimd_u8_t u8[64];
    } shorter_members, shorter_member, longer_members, broadcast_first, rotate_first;

    // We will be walking through batches of elements from the shorter array,
    // and comparing them with the longer array. The first element of the shorter
    // array will be broadcasted to a vector. As there is no cheap `_mm512_permutexvar_epi16`-like
    // instruction in AVX512, we couldt try using `vpshufb` via `_mm512_shuffle_epi8`,
    // to load the 1st and 2nd byte of the first element into each 16-bit word,
    // but it only works within 128-bit lanes.
    // For comparison:
    //
    //  - `_mm512_permutex_epi64` - needs F - 3 cycles latency
    //  - `_mm512_shuffle_epi8` - needs BW - 1 cycle latency
    //  - `_mm512_permutexvar_epi16` - needs BW - 4-6 cycles latency
    //  - `_mm512_permutexvar_epi8` - needs VBMI - 3 cycles latency
    broadcast_first.zmm = _mm512_set1_epi32(0x03020100);

simsimd_intersect_u32_ice_cycle:
    // At any given cycle, make sure we use the shorter array as the first array.
    if (shorter_remaining > longer_remaining) {
        simsimd_u32_t const* temp_array = shorter;
        shorter = longer, longer = temp_array;
        simsimd_size_t temp_length = shorter_length;
        shorter_length = longer_length, longer_length = temp_length;
        simsimd_size_t temp_remaining = shorter_remaining;
        shorter_remaining = longer_remaining, longer_remaining = temp_remaining;
    }

    // Estimate if we need to load the whole array or just a part of it.
    shorter_load_size = shorter_remaining < 16 ? shorter_remaining : 16;
    shorter_mask = (__mmask16)_bzhi_u32(0xFFFF, shorter_remaining);
    longer_load_size = longer_remaining < 16 ? longer_remaining : 16;
    longer_mask = (__mmask16)_bzhi_u32(0xFFFF, longer_remaining);
    shorter_members.zmm = _mm512_maskz_loadu_epi32(shorter_mask, (__m512i const*)shorter);
    longer_members.zmm = _mm512_maskz_loadu_epi32(longer_mask, (__m512i const*)longer);

    // Now that we've loaded the chunks, compare them sliding through them.
    do {
        shorter_member.zmm = _mm512_permutexvar_epi8(broadcast_first.zmm, shorter_members.zmm);

        // Compare `shorter_member` with each element in `longer_members.zmm`,
        // and jump to the position of the match. There can be only one match at most!
        equal_mask = _mm512_mask_cmpeq_epu32_mask(longer_mask, shorter_member.zmm, longer_members.zmm);
        simsimd_size_t equal_count = equal_mask != 0;
        intersection_count += equal_count;

        // When comparing a scalar against a sorted array, we can find three types of elements:
        // - entries that scalar is greater than,
        // - entries that scalar is equal to,
        // - entries that scalar is less than,
        // ... in that order! Any of them can be an empty set.
        greater_mask = _mm512_mask_cmplt_epu32_mask(longer_mask, longer_members.zmm, shorter_member.zmm);
        int greater_count = _mm_popcnt_u32(greater_mask);
        int smaller_exists = longer_load_size > greater_count - equal_count;
        int shorter_will_advance = equal_count | smaller_exists;

        // Advance the first array:
        // - to the next element, if a match was found,
        // - to the next element, if the current element is smaller than any elements in the second array.
        shorter += shorter_will_advance;
        shorter_load_size -= shorter_will_advance;

        // Advance the second array:
        // - to the next element after match, if a match was found,
        // - to the first element that is greater than the current element in the first array, if no match was found.
        longer += greater_count + equal_count;
        longer_load_size -= greater_count + equal_count;

    } while (!_ktestz_mask16_u8(shorter_mask, longer_mask));

simsimd_intersect_u32_ice_cycle_longer:
    //
    if (longer_remaining < 16) {
        longer_load_size = longer_remaining;
        longer_mask = (__mmask16)_bzhi_u32(0xFFFF, longer_remaining);
    } else {
        longer_load_size = 16;
        longer_mask = 0xFFFF;
    }
    longer_members.zmm = _mm512_maskz_loadu_epi32(longer_mask, (__m512i const*)(longer + longer_idx));

    if (shorter_will_advance) {
        if (shorter_load_size) {
            --shorter_load_size;
            shorter_members.zmm = _mm512_alignr_epi32(shorter_members.zmm, shorter_members.zmm, 1);
            goto simsimd_intersect_u32_ice_cycle_longer;
        }
        if (shorter_idx < shorter_length && longer_idx < longer_length)
            goto simsimd_intersect_u32_ice_cycle;
    } else {
        if (longer_idx < longer_length)
            goto simsimd_intersect_u32_ice_cycle_longer;
    }

    *results = intersection_count;
}

#pragma clang attribute pop
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_SKYLAKE
#endif // SIMSIMD_TARGET_X86

#if SIMSIMD_TARGET_ARM
#if SIMSIMD_TARGET_SVE

#pragma GCC push_options
#pragma GCC target("arch=armv8.2-a+sve")
#pragma clang attribute push(__attribute__((target("arch=armv8.2-a+sve"))), apply_to = function)

SIMSIMD_PUBLIC void simsimd_intersect_u16_sve(simsimd_u16_t const* shorter, simsimd_u16_t const* longer,
                                              simsimd_size_t shorter_length, simsimd_size_t longer_length,
                                              simsimd_distance_t* results) {

    // SVE implementations with 128-bit registers can only fit 8x 16-bit words,
    // making this kernel quite inefficient. Let's aim for registers of 256 bits and larger.
    simsimd_size_t longer_load_size = svcnth();
    if (longer_load_size < 16) {
        simsimd_intersect_u16_serial(shorter, longer, shorter_length, longer_length, results);
        return;
    }

    simsimd_size_t intersection_count = 0;
    simsimd_size_t shorter_idx = 0, longer_idx = 0;
    while (shorter_idx < shorter_length && longer_idx < longer_length) {
        // Load `shorter_member` and broadcast it, load `longer_members_vec` from memory
        simsimd_size_t longer_remaining = longer_length - longer_idx;
        simsimd_u16_t shorter_member = shorter[shorter_idx];
        svbool_t pg = svwhilelt_b16_u64(longer_idx, longer_length);
        svuint16_t shorter_member_vec = svdup_n_u16(shorter_member);
        svuint16_t longer_members_vec = svld1_u16(pg, longer + longer_idx);

        // Compare `shorter_member` with each element in `longer_members_vec`
        svbool_t equal_mask = svcmpeq_u16(pg, shorter_member_vec, longer_members_vec);
        simsimd_size_t equal_count = svcntp_b16(svptrue_b16(), equal_mask);
        intersection_count += equal_count;

        // Count the number of elements in `longer_members_vec` that are less than `shorter_member`
        svbool_t smaller_mask = svcmplt_u16(pg, longer_members_vec, shorter_member_vec);
        simsimd_size_t smaller_count = svcntp_b16(svptrue_b16(), smaller_mask);

        // Advance pointers
        longer_load_size = longer_remaining < longer_load_size ? longer_remaining : longer_load_size;
        shorter_idx += (longer_load_size - smaller_count - equal_count) != 0;
        longer_idx += smaller_count + equal_count;

        // Swap arrays if necessary
        if ((shorter_length - shorter_idx) > (longer_length - longer_idx)) {
            simsimd_u16_t const* temp_array = shorter;
            shorter = longer, longer = temp_array;
            simsimd_size_t temp_length = shorter_length;
            shorter_length = longer_length, longer_length = temp_length;
            simsimd_size_t temp_idx = shorter_idx;
            shorter_idx = longer_idx, longer_idx = temp_idx;
        }
    }
    *results = intersection_count;
}

SIMSIMD_PUBLIC void simsimd_intersect_u32_sve(simsimd_u32_t const* shorter, simsimd_u32_t const* longer,
                                              simsimd_size_t shorter_length, simsimd_size_t longer_length,
                                              simsimd_distance_t* results) {

    // SVE implementations with 128-bit registers can only fit 4x 32-bit words,
    // making this kernel quite inefficient. Let's aim for registers of 256 bits and larger.
    simsimd_size_t longer_load_size = svcntw();
    if (longer_load_size < 8) {
        simsimd_intersect_u32_serial(shorter, longer, shorter_length, longer_length, results);
        return;
    }

    simsimd_size_t intersection_count = 0;
    simsimd_size_t shorter_idx = 0, longer_idx = 0;
    while (shorter_idx < shorter_length && longer_idx < longer_length) {
        // Load `shorter_member` and broadcast it, load `longer_members_vec` from memory
        simsimd_size_t longer_remaining = longer_length - longer_idx;
        simsimd_u32_t shorter_member = shorter[shorter_idx];
        svbool_t pg = svwhilelt_b32_u64(longer_idx, longer_length);
        svuint32_t shorter_member_vec = svdup_n_u32(shorter_member);
        svuint32_t longer_members_vec = svld1_u32(pg, longer + longer_idx);

        // Compare `shorter_member` with each element in `longer_members_vec`
        svbool_t equal_mask = svcmpeq_u32(pg, shorter_member_vec, longer_members_vec);
        simsimd_size_t equal_count = svcntp_b32(svptrue_b32(), equal_mask);
        intersection_count += equal_count;

        // Count the number of elements in `longer_members_vec` that are less than `shorter_member`
        svbool_t smaller_mask = svcmplt_u32(pg, longer_members_vec, shorter_member_vec);
        simsimd_size_t smaller_count = svcntp_b32(svptrue_b32(), smaller_mask);

        // Advance pointers
        longer_load_size = longer_remaining < longer_load_size ? longer_remaining : longer_load_size;
        shorter_idx += (longer_load_size - smaller_count - equal_count) != 0;
        longer_idx += smaller_count + equal_count;

        // Swap arrays if necessary
        if ((shorter_length - shorter_idx) > (longer_length - longer_idx)) {
            simsimd_u32_t const* temp_array = shorter;
            shorter = longer, longer = temp_array;
            simsimd_size_t temp_length = shorter_length;
            shorter_length = longer_length, longer_length = temp_length;
            simsimd_size_t temp_idx = shorter_idx;
            shorter_idx = longer_idx, longer_idx = temp_idx;
        }
    }
    *results = intersection_count;
}

#pragma clang attribute pop
#pragma GCC pop_options
#endif // SIMSIMD_TARGET_SVE
#endif // SIMSIMD_TARGET_ARM

#ifdef __cplusplus
}
#endif

#endif
