/**
 * @file fits_core.c
 * @brief Core engine for opening FITS files, scanning HDU blocks, and decoding
 * binary data.
 *
 * DESIGN PRINCIPLES:
 * 1. Zero-Copy I/O: Uses POSIX memory-mapping (mmap) to map the disk file
 * directly into process virtual address space. This makes initial file opening
 * nearly instantaneous even for gigabyte-scale telescope files.
 * 2. HDU Traversal: Iterates sequentially through Header-Data Units.
 *    - Header size = N * 2880 bytes (scanned until 'END' keyword).
 *    - Data size = Padded up to the nearest multiple of 2880 bytes.
 * 3. Endianness Correction: Uses fits_swapXX_array() after loading binary
 * payload to translate Big-Endian disk representation into host Little-Endian
 * CPU layout.
 */

#include "../include/fits_core.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


/* =========================================================================
 * PRIVATE HELPER FUNCTIONS
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
 * @brief Scans a raw mmap buffer to parse the data descriptor (BITPIX, NAXIS)
 * for an HDU.
 */
static int parse_hdu_descriptor(FitsHDU *hdu) {
  FitsDataDescriptor *desc = &hdu->data_desc;
  memset(desc, 0, sizeof(FitsDataDescriptor));

  // 1. Get BITPIX (Data Type)
  FitsCard *bitpix_card = fits_header_find_card(&hdu->header, "BITPIX");
  if (!bitpix_card) {
    return -1; // Mandatory BITPIX keyword missing
  }
  desc->bitpix = atoi(bitpix_card->value);

  // 2. Get NAXIS (Number of Dimensions)
  FitsCard *naxis_card = fits_header_find_card(&hdu->header, "NAXIS");
  if (!naxis_card) {
    return -1; // Mandatory NAXIS keyword missing
  }
  desc->naxis = atoi(naxis_card->value);

  // 3. Read Axis Dimensions (NAXIS1, NAXIS2, ...)
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

  // 4. Calculate total unpadded binary data bytes
  size_t bytes_per_element = 0;
  switch (abs(desc->bitpix)) {
  case 8:
    bytes_per_element = 1;
    break;
  case 16:
    bytes_per_element = 2;
    break;
  case 32:
    bytes_per_element = 4;
    break;
  case 64:
    bytes_per_element = 8;
    break;
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
 * @brief Opens a FITS file using mmap and parses all HDU headers sequentially.
 */
FitsFile *fits_open(const char *filepath) {
  if (!filepath)
    return NULL;

  // 1. Open File Descriptor
  int fd = open(filepath, O_RDONLY);
  if (fd < 0) {
    perror("[libfits] Error opening file");
    return NULL;
  }

  // 2. Query File Size
  struct stat st;
  if (fstat(fd, &st) < 0 || st.st_size == 0) {
    close(fd);
    return NULL;
  }

  // 3. Memory-Map File
  void *mmap_ptr = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd); // File descriptor no longer needed after mmap succeeds

  if (mmap_ptr == MAP_FAILED) {
    perror("[libfits] mmap failed");
    return NULL;
  }

  // 4. Allocate FitsFile Context
  FitsFile *file = (FitsFile *)calloc(1, sizeof(FitsFile));
  if (!file) {
    munmap(mmap_ptr, st.st_size);
    return NULL;
  }

  file->filepath = strdup(filepath);
  file->mmap_addr = mmap_ptr;
  file->file_size = (size_t)st.st_size;
  file->hdu_count = 0;
  file->hdus = NULL;

  // 5. Traverse Sequential HDUs
  size_t offset = 0;
  size_t hdu_capacity = 4;
  file->hdus = (FitsHDU *)calloc(hdu_capacity, sizeof(FitsHDU));

  while (offset < file->file_size) {
    size_t header_start = offset;
    size_t header_bytes = 0;
    int found_end = 0;

    // Scan sequential 2880-byte header blocks until "END" keyword card is found
    while (offset + FITS_BLOCK_SIZE <= file->file_size) {
      const char *block_ptr = (const char *)mmap_ptr + offset;
      header_bytes += FITS_BLOCK_SIZE;
      offset += FITS_BLOCK_SIZE;

      // Check each 80-char record in this 2880-byte block for "END"
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
      break; // Reached EOF or corrupt header without END

    // Resize HDU array if needed
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

    // Parse Header Block Cards
    const char *header_src = (const char *)mmap_ptr + header_start;
    if (fits_parse_header_block(header_src, header_bytes,
                                &current_hdu->header) != 0) {
      break;
    }

    // Extract Data Descriptor & Set File Data Offset
    parse_hdu_descriptor(current_hdu);
    current_hdu->data_desc.data_offset = offset;

    // Advance file offset past the padded binary data payload
    size_t padded_data_bytes =
        pad_to_fits_block(current_hdu->data_desc.data_bytes);
    offset += padded_data_bytes;

    file->hdu_count++;
  }

  return file;
}

/**
 * @brief Allocates heap memory and copies binary payload, applying
 * byte-swapping.
 */
int fits_load_data(FitsHDU *hdu, FitsFile *file_handle) {
  if (!hdu || !file_handle || hdu->data_desc.data_bytes == 0) {
    return -1;
  }

  // Return if already loaded
  if (hdu->is_data_loaded && hdu->raw_data != NULL) {
    return 0;
  }

  // Verify bounds against mmap size
  size_t start_offset = hdu->data_desc.data_offset;
  size_t byte_count = hdu->data_desc.data_bytes;

  if (start_offset + byte_count > file_handle->file_size) {
    return -2; // Data extends past file end
  }

  // Allocate Heap Memory for Python/Host use
  void *buffer = malloc(byte_count);
  if (!buffer)
    return -3;

  // Copy raw Big-Endian bytes from mmap file view
  const void *mmap_data_src =
      (const char *)file_handle->mmap_addr + start_offset;
  memcpy(buffer, mmap_data_src, byte_count);

  // Apply Endianness Byte-Swapping based on BITPIX
  int bitpix = hdu->data_desc.bitpix;
  size_t element_count = byte_count / (abs(bitpix) / 8);

  switch (bitpix) {
  case 8:
    // 8-bit unsigned integer: No byte swapping required
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
    return -4; // Unsupported BITPIX
  }

  hdu->raw_data = buffer;
  hdu->is_data_loaded = 1;

  return 0; // Success
}

/**
 * @brief Frees all dynamic memory, unmaps file, and closes context.
 */
void fits_close(FitsFile *file) {
  if (!file)
    return;

  // Free HDU resources
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

  // Unmap file memory
  if (file->mmap_addr && file->file_size > 0) {
    munmap(file->mmap_addr, file->file_size);
    file->mmap_addr = NULL;
  }

  if (file->filepath) {
    free(file->filepath);
    file->filepath = NULL;
  }

  free(file);
}