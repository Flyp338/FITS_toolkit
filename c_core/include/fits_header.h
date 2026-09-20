/**
 * @file fits_header.h
 * @brief Structures and functions for parsing FITS ASCII header units.
 *
 * Every FITS header block consists of sequential 80-character card images
 * formatted as: KEYWORD = VALUE / COMMENT. Headers are terminated by the "END"
 * keyword and padded with spaces up to a 2880-byte block boundary.
 */

#ifndef FITS_HEADER_H
#define FITS_HEADER_H

#include <stddef.h>
#include <stdint.h>


#define FITS_RECORD_SIZE 80  ///< Standard fixed size of one FITS header card
#define FITS_BLOCK_SIZE 2880 ///< Fixed byte block boundary in FITS standard

/**
 * @brief Represents a single parsed key-value-comment card entry from a FITS
 * header.
 */
typedef struct {
  char key[9];    ///< Null-terminated keyword string (up to 8 uppercase chars)
  char value[72]; ///< Cleaned string representation of value (quotes/spaces
                  ///< trimmed)
  char comment[72]; ///< Optional comment field after the '/' separator
} FitsCard;

/**
 * @brief Dynamic array container for all key-value cards in a single HDU
 * header.
 */
typedef struct {
  FitsCard *cards; ///< Pointer to dynamically allocated array of FitsCard items
  size_t count;    ///< Current number of parsed cards
  size_t capacity; ///< Allocated storage capacity for cards
} FitsHeader;

/**
 * @brief Parses raw 2880-byte text blocks into structured FitsHeader memory.
 *
 * @param header_buffer Pointer to raw ASCII memory buffer containing header
 * data.
 * @param buffer_size Size of header buffer in bytes (must be a multiple of
 * 2880).
 * @param out_header Pointer to a FitsHeader struct to populate.
 * @return int 0 on success, non-zero error code on failure.
 */
int fits_parse_header_block(const char *header_buffer, size_t buffer_size,
                            FitsHeader *out_header);

/**
 * @brief Searches a FitsHeader struct for a specific keyword.
 *
 * @param header Pointer to populated FitsHeader struct.
 * @param key Case-insensitive keyword to look up (e.g., "BITPIX", "NAXIS1").
 * @return FitsCard* Pointer to matching card if found, or NULL if keyword
 * missing.
 */
FitsCard *fits_header_find_card(const FitsHeader *header, const char *key);

/**
 * @brief Helper utility to safely free memory allocated for header cards.
 * @param header Pointer to FitsHeader struct to clean up.
 */
void fits_free_header(FitsHeader *header);

#endif // FITS_HEADER_H