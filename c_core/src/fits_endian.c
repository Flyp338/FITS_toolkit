/**
 * @file fits_endian.c
 * @brief High-performance byte-swapping utility routines for FITS binary data.
 *
 * WHY THIS FILE EXISTS:
 * The FITS standard (NASA/IAU) specifies Big-Endian byte ordering (most
 * significant byte first) for all multi-byte numerical values stored on disk
 * (BITPIX = 16, 32, 64, -32, -64). However, nearly all modern consumer and
 * server processors (x86_64, ARM64) use Little-Endian byte ordering (least
 * significant byte first).
 *
 * To read multi-byte values accurately in C/Python, we must reverse the byte
 * order of each element in-place immediately after loading raw bytes from disk
 * into memory.
 */

#include "../include/fits_endian.h"

/* =========================================================================
 * COMPILER INTRINSIC DETECTION
 * Modern GCC/Clang compilers provide builtin byte-swapping functions that
 * compile directly down to single assembly instructions (e.g., `bswap` on x86,
 * `rev` on ARM).
 * ========================================================================= */

#if defined(__GNUC__) || defined(__clang__)
#define SWAP16(x) __builtin_bswap16((uint16_t)(x))
#define SWAP32(x) __builtin_bswap32((uint32_t)(x))
#define SWAP64(x) __builtin_bswap64((uint64_t)(x))
#elif defined(_MSC_VER)
#include <stdlib.h>
#define SWAP16(x) _byteswap_ushort((uint16_t)(x))
#define SWAP32(x) _byteswap_ulong((uint32_t)(x))
#define SWAP64(x) _byteswap_uint64((uint64_t)(x))
#else
/* Portable bit-shift fallbacks if no intrinsic is available */
static inline uint16_t SWAP16(uint16_t val) { return (val >> 8) | (val << 8); }
static inline uint32_t SWAP32(uint32_t val) {
  return ((val >> 24) & 0x000000FF) | ((val >> 8) & 0x0000FF00) |
         ((val << 8) & 0x00FF0000) | ((val << 24) & 0xFF000000);
}
static inline uint64_t SWAP64(uint64_t val) {
  return ((val >> 56) & 0x00000000000000FFULL) |
         ((val >> 40) & 0x000000000000FF00ULL) |
         ((val >> 24) & 0x0000000000FF0000ULL) |
         ((val >> 8) & 0x00000000FF000000ULL) |
         ((val << 8) & 0x0000002400000000ULL) |
         ((val << 24) & 0x0000FF0000000000ULL) |
         ((val << 40) & 0x00FF000000000000ULL) |
         ((val << 56) & 0xFF00000000000000ULL);
}
#endif

/**
 * @brief Swaps bytes for an array of 16-bit integers (BITPIX = 16).
 *
 * Reverses 2-byte pairs: [ByteA, ByteB] -> [ByteB, ByteA]
 *
 * @param buffer Pointer to raw memory containing 16-bit elements.
 * @param count Number of 16-bit elements in the buffer.
 */
void fits_swap16_array(void *buffer, size_t count) {
  if (!buffer || count == 0)
    return;

  uint16_t *ptr = (uint16_t *)buffer;
  for (size_t i = 0; i < count; i++) {
    ptr[i] = SWAP16(ptr[i]);
  }
}

/**
 * @brief Swaps bytes for an array of 32-bit elements (BITPIX = 32 or BITPIX =
 * -32).
 *
 * Works identically for both 32-bit signed integers and 32-bit IEEE 754
 * floating-point values because byte reversal operates at the bit level
 * regardless of representation.
 *
 * Reverses 4-byte quads: [B0, B1, B2, B3] -> [B3, B2, B1, B0]
 *
 * @param buffer Pointer to raw memory containing 32-bit elements.
 * @param count Number of 32-bit elements in the buffer.
 */
void fits_swap32_array(void *buffer, size_t count) {
  if (!buffer || count == 0)
    return;

  uint32_t *ptr = (uint32_t *)buffer;
  for (size_t i = 0; i < count; i++) {
    ptr[i] = SWAP32(ptr[i]);
  }
}

/**
 * @brief Swaps bytes for an array of 64-bit elements (BITPIX = 64 or BITPIX =
 * -64).
 *
 * Works for 64-bit signed integers and 64-bit IEEE 754 double precision floats.
 *
 * Reverses 8-byte blocks: [B0, B1, B2, B3, B4, B5, B6, B7] -> [B7, B6, B5, B4,
 * B3, B2, B1, B0]
 *
 * @param buffer Pointer to raw memory containing 64-bit elements.
 * @param count Number of 64-bit elements in the buffer.
 */
void fits_swap64_array(void *buffer, size_t count) {
  if (!buffer || count == 0)
    return;

  uint64_t *ptr = (uint64_t *)buffer;
  for (size_t i = 0; i < count; i++) {
    ptr[i] = SWAP64(ptr[i]);
  }
}