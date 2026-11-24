/**
 * newos/doc.h — Runtime Documentale & Proofing per NewSoft SDK
 * Portato dalle logiche di WordCraft.Fluent (WordCraftDocumentState / ProofingAndToolsService)
 */
#ifndef NEWOS_SDK_DOC_H
#define NEWOS_SDK_DOC_H

#ifndef TYPES_H
#include "types.h"
#endif

typedef struct {
    int32_t chars;
    int32_t chars_no_spaces;
    int32_t words;
    int32_t lines;
} ns_doc_stats_t;

static inline void ns_doc_compute_stats(const char* text, ns_doc_stats_t* out) {
    if (!out) return;
    out->chars = 0;
    out->chars_no_spaces = 0;
    out->words = 0;
    out->lines = 1;
    if (!text) return;

    bool in_word = false;
    for (int i = 0; text[i]; i++) {
        char c = text[i];
        out->chars++;
        if (c == '\n') out->lines++;
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            in_word = false;
        } else {
            out->chars_no_spaces++;
            if (!in_word) {
                out->words++;
                in_word = true;
            }
        }
    }
}

static inline void ns_doc_to_upper(char* text) {
    if (!text) return;
    for (int i = 0; text[i]; i++) {
        if (text[i] >= 'a' && text[i] <= 'z') text[i] = (char)(text[i] - 32);
    }
}

static inline void ns_doc_to_lower(char* text) {
    if (!text) return;
    for (int i = 0; text[i]; i++) {
        if (text[i] >= 'A' && text[i] <= 'Z') text[i] = (char)(text[i] + 32);
    }
}

#endif /* NEWOS_SDK_DOC_H */
