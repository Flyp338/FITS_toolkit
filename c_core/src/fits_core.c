/**
 * @file fits_core.c
 * @brief Core engine for opening FITS files, scanning HDU blocks, and decoding
 * binary data.
 *
 * ARCHITECTURE & DESIGN PRINCIPLES:
 * 1. Cross-Platform Memory Mapping:
 *    - On POSIX (Linux/macOS): Uses `open()`, `fstat()`, and `mmap()`.
 *    - On Windows (MSVC/MinGW): Uses `CreateFileA()`, `GetFileSizeEx()`, and
 * `CreateFileMappingA()`. This ensures high-performance zero-copy file mapping
 * on all operating systems without triggering missing `<sys/mman.h>`
 * compilation errors on Windows.
 *
 * 2. Sequential HDU Scanning:
 *    - Iterates block by block (2880 bytes per block) across the file.
 *    - Parses ASCII headers card by card (80 bytes per card) until reaching the
 * "END" card.
 *    - Reads HDU descriptor cards (`BITPIX`, `NAXIS`, `NAXIS1`, `NAXIS2`,
 * etc.).
 *    - Skips padded binary data blocks to land precisely on the next HDU header
 * start.
 *
 * 3. Binary Payload Loading & Endianness Correction:
 *    - Allocates heap memory for the requested HDU data array.
 *    - Copies raw bytes from the mapped file view.
 *    - Calls `fits_swapXX_array()` from `fits_endian.h` to convert Big-Endian
 *      disk bytes into native host Little-Endian CPU representations.
 */

#include "../include/fits_core.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* =========================================================================
 * CROSS-PLATFORM MEMORY MAPPING INCLUDES & WRAPPERS
 * ========================================================================= */

#ifdef _WIN32
#include <io.h>
#include <windows.h>

#else
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#endif

/**
 * @brief Cross-platform helper to memory-map a target file into read-only
 * virtual address space.
 *
 * @param filepath Path to the target FITS file.
 * @param out_size Output pointer receiving the total byte size of the mapped
 * file.
 * @return void* Pointer to mapped memory buffer, or NULL on failure.
 */
static void *map_file_to_memory(const char *filepath, size_t *out_size) {
#ifdef _WIN32
  // Windows API implementation
  HANDLE file_handle =
      CreateFileA(filepath, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL, NULL);
  if (file_handle == INVALID_HANDLE_VALUE) {
    return NULL;
  }

  LARGE_INTEGER file_size;
  if (!GetFileSizeEx(file_handle, &file_size)) {
    CloseHandle(file_handle);
    return NULL;
  }
  *out_size = (size_t)file_size.QuadPart;

  HANDLE mapping_handle =
      CreateFileMappingA(file_handle, NULL, PAGE_READONLY, 0, 0, NULL);
  CloseHandle(file_handle); // File handle is no longer needed after mapping
                            // handle creation

  if (!mapping_handle) {
    return NULL;
  }

  void *ptr = MapViewOfFile(mapping_handle, FILE_MAP_READ, 0, 0, 0);
  CloseHandle(
      mapping_handle); // Mapping handle can be closed once view is mapped
  return ptr;

#else
  // POSIX mmap implementation
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    return NULL;
  }

  struct stat st;
  if (fstat(fd, &st) < 0 || st.st_size == 0) {
    close(fd);
    return NULL;
  }
  *out_size = (size_t)st.st_size;

  void *ptr = mmap(NULL, *out_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd); // File descriptor can be closed once mmap succeeds

  if (ptr == MAP_FAILED) {
    return NULL;
  }

  return ptr;
#endif
}

/**
 * @brief Cross-platform helper to unmap a previously mapped file buffer.
 *
 * @param ptr Pointer to mapped memory buffer.
 * @param size Size of mapped buffer in bytes.
 */
static void unmap_file_memory(void *ptr, size_t size) {
  if (!ptr)
    return;
#ifdef _WIN32
  UnmapViewOfFile(ptr);
#else
  munmap(ptr, size);
#endif
}

/* =========================================================================
 * PRIVATE HELPER ROUTINES
 * ========================================================================= */

/**
 * @brief Rounds up a byte count to the next 2880-byte FITS block boundary.
 */
static size_t pad_to_fits_block(size_t size) {
  if (size == 0)
    return 0;
  size_t remainder = size % FITS_BLOCK_SIZE;
  if (remainder == 0)
    return size;
  return size + (FITS_BLOCK_SIZE - remainder);
}

/**
 * @brief Extracts BITPIX, NAXIS, and axis dimensions from an HDU header struct.
 *
 * @param hdu Pointer to HDU whose header has already been parsed into cards.
 * @return int 0 on success, non-zero if mandatory header keywords are missing.
 */
static int parse_hdu_descriptor(FitsHDU *hdu) {
  FitsDataDescriptor *desc = &hdu->data_desc;
  memset(desc, 0, sizeof(FitsDataDescriptor));

  // 1. Get BITPIX (Data Type)
  FitsCard *bitpix_card = fits_header_find_card(&hdu->header, "BITPIX");
  if (!bitpix_card) {
    return -1; // Mandatory BITPIX card missing
  }
  desc->bitpix = atoi(bitpix_card->value);

  // 2. Get NAXIS (Number of dimensions)
  FitsCard *naxis_card = fits_header_find_card(&hdu->header, "NAXIS");
  if (!naxis_card) {
    return -1; // Mandatory NAXIS card missing
  }
  desc->naxis = atoi(naxis_card->value);

  // 3. Get individual axis lengths (NAXIS1, NAXIS2, ... NAXIS9)
  size_t total_elements = (desc->naxis > 0) ? 1 : 0;
  for (int i = 0; i < desc->naxis && i < 9; i++) {
    char axis_key[9];
    snprintf(axis_key, sizeof(axis_key), "NAXIS%d", i + 1);
    FitsCard *axis_card = fits_header_find_card(&hdu->header, axis_key);
    if (axis_card) {
      desc->naxis_dims[i] = atoll(axis_card->value);
      total_elements *= (size_t)desc->naxis_dims[i];
    } else {
      desc->naxis_dims[i] = 0;
    }
  }

  // 4. Calculate raw byte size of the data block
  size_t bytes_per_element = 0;
  switch (abs(desc->bitpix)) {
  case 8:
    bytes_per_element = 1;
    break; // 8-bit unsigned integer
  case 16:
    bytes_per_element = 2;
    break; // 16-bit signed integer
  case 32:
    bytes_per_element = 4;
    break; // 32-bit signed integer / float
  case 64:
    bytes_per_element = 8;
    break; // 64-bit signed integer / double
  default:
    bytes_per_element = 0;
    break;
  }

  desc->data_bytes = total_elements * bytes_per_element;
  return 0;
}

/* =========================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================= */

/**
 * @brief Opens a FITS file using cross-platform memory mapping and parses
 * sequential HDUs.
 */
FitsFile *fits_open(const char *filepath) {
  if (!filepath)
    return NULL;

  size_t file_size = 0;
  void *mmap_ptr = map_file_to_memory(filepath, &file_size);
  if (!mmap_ptr) {
    fprintf(stderr, "[libfits] Error: Unable to open or memory-map file '%s'\n",
            filepath);
    return NULL;
  }

  FitsFile *file = (FitsFile *)calloc(1, sizeof(FitsFile));
  if (!file) {
    unmap_file_memory(mmap_ptr, file_size);
    return NULL;
  }

  file->filepath = strdup(filepath);
  file->mmap_addr = mmap_ptr;
  file->file_size = file_size;
  file->hdu_count = 0;
  file->hdus = NULL;

  size_t offset = 0;
  size_t hdu_capacity = 4;
  file->hdus = (FitsHDU *)calloc(hdu_capacity, sizeof(FitsHDU));

  // Traverse sequential HDU units across the file
  while (offset < file->file_size) {
    size_t header_start = offset;
    size_t header_bytes = 0;
    int found_end = 0;

    // Scan sequential 2880-byte header blocks until "END" card is detected
    while (offset + FITS_BLOCK_SIZE <= file->file_size) {
      const char *block_ptr = (const char *)mmap_ptr + offset;
      header_bytes += FITS_BLOCK_SIZE;
      offset += FITS_BLOCK_SIZE;

      for (int card_idx = 0; card_idx < 36; card_idx++) {
        const char *card = block_ptr + (card_idx * FITS_RECORD_SIZE);
        if (strncmp(card, "END ", 4) == 0 || strncmp(card, "END\0", 4) == 0) {
          found_end = 1;
          break;
        }
      }
      if (found_end)
        break;
    }

    if (!found_end)
      break; // Malformed file or EOF reached without "END"

    // Dynamically resize HDU container array if capacity is reached
    if (file->hdu_count >= hdu_capacity) {
      hdu_capacity *= 2;
      FitsHDU *new_hdus =
          (FitsHDU *)realloc(file->hdus, hdu_capacity * sizeof(FitsHDU));
      if (!new_hdus)
        break;
      file->hdus = new_hdus;
    }

    FitsHDU *current_hdu = &file->hdus[file->hdu_count];
    memset(current_hdu, 0, sizeof(FitsHDU));

    // Parse header cards into structured memory
    const char *header_src = (const char *)mmap_ptr + header_start;
    if (fits_parse_header_block(header_src, header_bytes,
                                &current_hdu->header) != 0) {
      break;
    }

    // Parse data attributes and store file offset to data payload
    parse_hdu_descriptor(current_hdu);
    current_hdu->data_desc.data_offset = offset;

    // Advance file offset past the 2880-byte padded binary data payload
    size_t padded_data_bytes =
        pad_to_fits_block(current_hdu->data_desc.data_bytes);
    offset += padded_data_bytes;

    file->hdu_count++;
  }

  return file;
}

/**
 * @brief Allocates heap memory for HDU binary payload and applies
 * byte-swapping.
 */
int fits_load_data(FitsHDU *hdu, FitsFile *file_handle) {
  if (!hdu || !file_handle || hdu->data_desc.data_bytes == 0) {
    return -1;
  }

  // Return successfully if data is already loaded
  if (hdu->is_data_loaded && hdu->raw_data != NULL) {
    return 0;
  }

  size_t start_offset = hdu->data_desc.data_offset;
  size_t byte_count = hdu->data_desc.data_bytes;

  if (start_offset + byte_count > file_handle->file_size) {
    return -2; // Data payload exceeds file boundaries
  }

  // Allocate heap memory buffer
  void *buffer = malloc(byte_count);
  if (!buffer)
    return -3;

  // Copy raw Big-Endian payload from mapped memory
  const void *mmap_data_src =
      (const char *)file_handle->mmap_addr + start_offset;
  memcpy(buffer, mmap_data_src, byte_count);

  // Apply Big-Endian -> Little-Endian byte-swapping based on BITPIX
  int bitpix = hdu->data_desc.bitpix;
  size_t element_count = byte_count / (abs(bitpix) / 8);

  switch (bitpix) {
  case 8:
    // 8-bit unsigned integer: No byte swap needed
    break;
  case 16:
    fits_swap16_array(buffer, element_count);
    break;
  case 32:
  case -32:
    fits_swap32_array(buffer, element_count);
    break;
  case 64:
  case -64:
    fits_swap64_array(buffer, element_count);
    break;
  default:
    free(buffer);
    return -4; // Unsupported or invalid BITPIX
  }

  hdu->raw_data = buffer;
  hdu->is_data_loaded = 1;

  return 0; // Success
}

/**
 * @brief Frees all allocated memory, unmaps file memory, and destroys file
 * context.
 */
void fits_close(FitsFile *file) {
  if (!file)
    return;

  // Free all HDU headers and loaded raw data arrays
  if (file->hdus) {
    for (size_t i = 0; i < file->hdu_count; i++) {
      fits_free_header(&file->hdus[i].header);
      if (file->hdus[i].raw_data) {
        free(file->hdus[i].raw_data);
        file->hdus[i].raw_data = NULL;
      }
    }
    free(file->hdus);
    file->hdus = NULL;
  }

  // Unmap memory view from virtual address space
  if (file->mmap_addr && file->file_size > 0) {
    unmap_file_memory(file->mmap_addr, file->file_size);
    file->mmap_addr = NULL;
  }

  if (file->filepath) {
    free(file->filepath);
    file->filepath = NULL;
  }

  free(file);
}