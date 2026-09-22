#include <stdio.h>
#include "../include/fits_core.h"

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <path-to-fits-file>\n", argv[0]);
        return 1;
    }

    const char *filepath = argv[1];
    printf("Opening FITS file: %s\n", filepath);

    FitsFile *file = fits_open(filepath);
    if (!file) {
        fprintf(stderr, "Failed to open FITS file.\n");
        return 1;
    }

    printf("Successfully mapped file! Found %zu HDU(s).\n\n", file->hdu_count);

    for (size_t i = 0; i < file->hdu_count; i++) {
        FitsHDU *hdu = &file->hdus[i];
        printf("--- HDU %zu ---\n", i);
        printf("Header Card Count: %zu\n", hdu->header.count);
        printf("BITPIX : %d\n", hdu->data_desc.bitpix);
        printf("NAXIS  : %d\n", hdu->data_desc.naxis);
        for (int d = 0; d < hdu->data_desc.naxis; d++) {
            printf("  NAXIS%d: %lld\n", d + 1, (long long)hdu->data_desc.naxis_dims[d]);
        }
        printf("Data Byte Offset: %zu\n", hdu->data_desc.data_offset);
        printf("Raw Data Size   : %zu bytes\n\n", hdu->data_desc.data_bytes);
    }

    fits_close(file);
    printf("Closed FITS file cleanly.\n");
    return 0;
}