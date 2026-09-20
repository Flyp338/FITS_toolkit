/**
 * @file fits_endian.h
 * @brief High-performance byte-swapping utility routines for FITS binary data.
 *
 * FITS specification mandates Big-Endian (Network Byte Order) encoding for all
 * multi-byte data types (BITPIX = 16, 32, 64, -32, -64). Modern x86_64 and ARM
 * architectures are Little-Endian. These functions convert FITS byte streams
 * into native machine representation in-place.
 */

#ifndef FITS_ENDIAN_H
#define FITS_ENDIAN_H

#include <stddef.h>
#include <stdint.h>


/**
 * @brief Reverses 16-bit integers in place (e.g., BITPIX = 16 short int).
 * @param buffer Pointer to the array of 16-bit elements.
 * @param count Number of 16-bit elements in the array.
 */
void fits_swap16_array(void *buffer, size_t count);

/**
 * @brief Reverses 32-bit integers and single-precision floats in place (BITPIX
 * = 32 or -32).
 * @param buffer Pointer to the array of 32-bit elements.
 * @param count Number of 32-bit elements in the array.
 */
void fits_swap32_array(void *buffer, size_t count);

/**
 * @brief Reverses 64-bit integers and double-precision floats in place (BITPIX
 * = 64 or -64).
 * @param buffer Pointer to the array of 64-bit elements.
 * @param count Number of 64-bit elements in the array.
 */
void fits_swap64_array(void *buffer, size_t count);

#endif // FITS_ENDIAN_H