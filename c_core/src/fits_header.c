/**
 * @file fits_header.c
 * @brief Implementation of FITS ASCII header unit parsing and keyword lookup.
 * 
 * FITS HEADER SPECIFICATION RECAP:
 * 1. Headers consist of sequential 80-character ASCII card images.
 * 2. Standard format: KEYWORD = VALUE / COMMENT
 *    - Columns 1-8  : Keyword (uppercase, padded with trailing spaces).
 *    - Columns 9-10 : Value indicator "= " (if a value is present).
 *    - Columns 11-80: Value representation and optional comment separated by '/'.
 * 3. Special cards (e.g., COMMENT, HISTORY, blank cards) do NOT contain an '=' sign.
 * 4. Strings in the VALUE field are enclosed in single quotes ('VALUE').
 * 5. Headers are terminated by the "END" keyword (cols 1-3) followed by spaces.
 * 6. Headers are padded with ASCII spaces (0x20) up to a 2880-byte block boundary.
 */

#include "../include/fits_header.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* =========================================================================
 * PRIVATE HELPER FUNCTIONS
 * ========================================================================= */

/**
 * @brief Trims leading and trailing whitespace from a C string in-place.
 */
static void trim_whitespace(char *str) {
    if (!str || *str == '\0') return;

    // Trim leading space
    char *start = str;
    while (isspace((unsigned char)*start)) start++;

    // Shift string left if leading spaces existed
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }

    // Trim trailing space
    size_t len = strlen(str);
    while (len > 0 && isspace((unsigned char)str[len - 1])) {
        str[--len] = '\0';
    }
}

/**
 * @brief Parses a single 80-character raw card buffer into a FitsCard struct.
 */
static void parse_single_card(const char *raw_card, FitsCard *out_card) {
    // Clear destination struct memory
    memset(out_card, 0, sizeof(FitsCard));

    // 1. Extract Keyword (Columns 1-8)
    char raw_key[9] = {0};
    strncpy(raw_key, raw_card, 8);
    raw_key[8] = '\0';
    trim_whitespace(raw_key);
    
    // Convert key to uppercase for standardized lookup
    for (int i = 0; raw_key[i] != '\0'; i++) {
        raw_key[i] = (char)toupper((unsigned char)raw_key[i]);
    }
    strncpy(out_card->key, raw_key, sizeof(out_card->key) - 1);

    // If card is shorter than standard or has no value assignment ('=' at col 9)
    if (raw_card[8] != '=' && raw_card[9] != ' ') {
        // Cards like COMMENT, HISTORY, or END place text directly in cols 9-80
        strncpy(out_card->comment, raw_card + 8, 71);
        out_card->comment[71] = '\0';
        trim_whitespace(out_card->comment);
        return;
    }

    // 2. Separate Value and Comment using the '/' character delimiter
    char payload[71] = {0};
    strncpy(payload, raw_card + 10, 70);
    payload[70] = '\0';

    char *slash_pos = strchr(payload, '/');
    if (slash_pos != NULL) {
        // Comment exists after the slash
        strncpy(out_card->comment, slash_pos + 1, sizeof(out_card->comment) - 1);
        trim_whitespace(out_card->comment);

        // Value is everything before the slash
        *slash_pos = '\0';
    }

    // 3. Clean up the Value string (trim quotes and spaces)
    trim_whitespace(payload);

    // Handle single-quoted FITS string values (e.g., 'NIRCam  ')
    if (payload[0] == '\'') {
        char *end_quote = strrchr(payload + 1, '\'');
        if (end_quote != NULL) {
            *end_quote = '\0'; // Strip trailing quote
            strncpy(out_card->value, payload + 1, sizeof(out_card->value) - 1);
        } else {
            // Malformed string, fallback to raw string
            strncpy(out_card->value, payload, sizeof(out_card->value) - 1);
        }
    } else {
        // Numeric (BITPIX, NAXIS), Boolean (T/F), or blank value
        strncpy(out_card->value, payload, sizeof(out_card->value) - 1);
    }

    trim_whitespace(out_card->value);
}


/* =========================================================================
 * PUBLIC API IMPLEMENTATION
 * ========================================================================= */

/**
 * @brief Parses raw 2880-byte header memory blocks into structured FitsHeader memory.
 */
int fits_parse_header_block(const char *header_buffer, size_t buffer_size, FitsHeader *out_header) {
    if (!header_buffer || !out_header || buffer_size == 0 || (buffer_size % FITS_BLOCK_SIZE != 0)) {
        return -1; // Invalid buffer or non-standard FITS block size
    }

    // Initialize initial dynamic array capacity
    out_header->count = 0;
    out_header->capacity = 36; // Default to 36 cards (exact count in 1 FITS block)
    out_header->cards = (FitsCard *)malloc(out_header->capacity * sizeof(FitsCard));

    if (!out_header->cards) {
        return -2; // Allocation failure
    }

    size_t total_cards = buffer_size / FITS_RECORD_SIZE;

    for (size_t i = 0; i < total_cards; i++) {
        const char *raw_card_ptr = header_buffer + (i * FITS_RECORD_SIZE);

        // Check if card dynamic array needs expansion
        if (out_header->count >= out_header->capacity) {
            size_t new_cap = out_header->capacity * 2;
            FitsCard *new_cards = (FitsCard *)realloc(out_header->cards, new_cap * sizeof(FitsCard));
            if (!new_cards) {
                fits_free_header(out_header);
                return -2; // Reallocation failure
            }
            out_header->cards = new_cards;
            out_header->capacity = new_cap;
        }

        // Parse 80-character line into structured FitsCard
        parse_single_card(raw_card_ptr, &out_header->cards[out_header->count]);
        
        FitsCard *current_card = &out_header->cards[out_header->count];
        out_header->count++;

        // Stop parsing immediately if "END" keyword is encountered
        if (strcmp(current_card->key, "END") == 0) {
            break;
        }
    }

    return 0; // Success
}

/**
 * @brief Searches a FitsHeader container for a specific keyword.
 */
FitsCard* fits_header_find_card(const FitsHeader *header, const char *key) {
    if (!header || !header->cards || !key) return NULL;

    // Convert requested search key to uppercase
    char search_key[9] = {0};
    strncpy(search_key, key, 8);
    for (int i = 0; search_key[i] != '\0'; i++) {
        search_key[i] = (char)toupper((unsigned char)search_key[i]);
    }

    // Linear search through header cards
    for (size_t i = 0; i < header->count; i++) {
        if (strcmp(header->cards[i].key, search_key) == 0) {
            return &header->cards[i];
        }
    }

    return NULL; // Keyword not found
}

/**
 * @brief Safely frees all dynamically allocated memory within a FitsHeader struct.
 */
void fits_free_header(FitsHeader *header) {
    if (!header) return;

    if (header->cards) {
        free(header->cards);
        header->cards = NULL;
    }
    header->count = 0;
    header->capacity = 0;
}