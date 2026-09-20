/**
 * @file fits_core.h
 * @brief Main API and memory-mapped file structures for the libfits C core
 * engine.
 *
 * Handles opening FITS files via memory mapping (mmap), iterating through
 * sequential Header-Data Units (HDUs), reading image dimensions, and exposing
 * binary data arrays.
 */

#ifndef FITS_CORE_H
#define FITS_CORE_H

#include "fits_endian.h"
#include "fits_header.h"


/**
 * @brief Describes the data payload dimensions and pixel representation of an
 * HDU.
 */
typedef struct {
  int bitpix; ///< Data type indicator: 8 (uint8), 16/32/64 (int), -32/-64
              ///< (float)
  int naxis;  ///< Number of dimensions (0 = header only, 2 = 2D image, 3 = data
              ///< cube)
  int64_t naxis_dims[9]; ///< Sizes for each axis (NAXIS1, NAXIS2, ... NAXIS9)
  size_t data_bytes;     ///< Total size of data array in raw bytes
  size_t data_offset;    ///< File byte offset where binary data stream starts
} FitsDataDescriptor;

/**
 * @brief Complete representation of a single Header-Data Unit (HDU).
 */
typedef struct {
  FitsHeader header;            ///< Parsed key-value metadata container
  FitsDataDescriptor data_desc; ///< Dimension and type descriptor
  void *raw_data; ///< Pointer to decoded binary pixel/tabular data in memory
  int is_data_loaded; ///< Flag: 1 if raw_data allocated/loaded, 0 otherwise
} FitsHDU;

/**
 * @brief Main file context handle returned when opening a .fits file.
 */
typedef struct {
  char *filepath;   ///< Absolute path to opened FITS file
  void *mmap_addr;  ///< Pointer to memory-mapped raw file stream
  size_t file_size; ///< Total size of file in bytes
  FitsHDU *hdus;    ///< Array of sequential HDUs found in file
  size_t hdu_count; ///< Number of parsed HDUs in file
} FitsFile;

/**
 * @brief Opens a FITS file and parses all header units across the entire
 * container.
 *
 * Uses mmap for zero-copy file inspection.
 *
 * @param filepath Path to the target .fits file.
 * @return FitsFile* Pointer to instantiated FitsFile handle, or NULL on error.
 */
FitsFile *fits_open(const char *filepath);

/**
 * @brief Loads and converts raw binary data for a target HDU into native
 * endianness.
 *
 * @param hdu Pointer to specific HDU to decode.
 * @param file_handle Pointer to active FitsFile handle.
 * @return int 0 on success, non-zero code on failure.
 */
int fits_load_data(FitsHDU *hdu, FitsFile *file_handle);

/**
 * @brief Frees all allocated memory, unmaps file from memory, and destroys file
 * handle.
 * @param file Pointer to FitsFile handle.
 */
void fits_close(FitsFile *file);

#endif // FITS_CORE_H