#include <string.h>
#include <types.h>

void* memset(void* dest, int val, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    uint8_t v = (uint8_t)val;
    for (size_t i = 0; i < count; i++) {
        d[i] = v;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < count; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t count) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < count; i++) {
            d[i] = s[i];
        }
    } else if (d > s) {
        for (size_t i = count; i > 0; i--) {
            d[i - 1] = s[i - 1];
        }
    }
    return dest;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

size_t strlen(const char* str) {
    size_t len = 0;
    while (str && str[len]) {
        len++;
    }
    return len;
}

char* strcpy(char* dest, const char* src) {
    char* orig = dest;
    while ((*dest++ = *src++));
    return orig;
}

char* strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dest[i] = src[i];
    }
    for (; i < n; i++) {
        dest[i] = '\0';
    }
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    while (n && *s1 && (*s1 == *s2)) {
        s1++;
        s2++;
        n--;
    }
    if (n == 0) return 0;
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

static inline char to_lower(char c) {
    if (c >= 'A' && c <= 'Z') return c + 32;
    return c;
}

int strcasecmp(const char* s1, const char* s2) {
    while (*s1 && (to_lower(*s1) == to_lower(*s2))) {
        s1++;
        s2++;
    }
    return (int)(unsigned char)to_lower(*s1) - (int)(unsigned char)to_lower(*s2);
}

char* strcat(char* dest, const char* src) {
    char* orig = dest;
    while (*dest) dest++;
    while ((*dest++ = *src++));
    return orig;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* orig = dest;
    while (*dest) dest++;
    while (n-- && *src) {
        *dest++ = *src++;
    }
    *dest = '\0';
    return orig;
}

char* strstr(const char* haystack, const char* needle) {
    if (!needle || !*needle) return (char*)haystack;
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n && (*h == *n)) {
            h++;
            n++;
        }
        if (!*n) return (char*)haystack;
    }
    return NULL;
}

char* strchr(const char* s, int c) {
    if (!s) return NULL;
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if ((char)c == '\0') return (char*)s;
    return NULL;
}

char* strrchr(const char* s, int c) {
    if (!s) return NULL;
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char*)s;
    return (char*)last;
}

void itoa(int64_t n, char* str, int base) {
    int i = 0;
    bool is_neg = false;

    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }

    if (n < 0 && base == 10) {
        is_neg = true;
        n = -n;
    }

    while (n != 0) {
        int rem = n % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        n = n / base;
    }

    if (is_neg) {
        str[i++] = '-';
    }
    str[i] = '\0';

    // Inverti la stringa
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}

void uitoa(uint64_t n, char* str, int base) {
    int i = 0;
    if (n == 0) {
        str[i++] = '0';
        str[i] = '\0';
        return;
    }
    while (n != 0) {
        int rem = n % base;
        str[i++] = (rem > 9) ? (rem - 10) + 'a' : rem + '0';
        n = n / base;
    }
    str[i] = '\0';
    for (int j = 0, k = i - 1; j < k; j++, k--) {
        char temp = str[j];
        str[j] = str[k];
        str[k] = temp;
    }
}

int mini_snprintf(char* buffer, size_t size, const char* format, ...) {
    if (!buffer || size == 0) return 0;

    __builtin_va_list args;
    __builtin_va_start(args, format);

    size_t bi = 0;
    for (size_t fi = 0; format[fi] != '\0' && bi < size - 1; fi++) {
        if (format[fi] == '%' && format[fi + 1] != '\0') {
            fi++;
            bool pad_zero = false;
            int min_width = 0;

            bool left_align = false;
            if (format[fi] == '-') {
                left_align = true;
                fi++;
            }
            if (format[fi] == '0') {
                pad_zero = true;
                fi++;
            }
            while (format[fi] >= '0' && format[fi] <= '9') {
                min_width = min_width * 10 + (format[fi] - '0');
                fi++;
            }

            if (format[fi] == 'd' || format[fi] == 'u' || format[fi] == 'x' || format[fi] == 'X') {
                char num_buf[32];
                if (format[fi] == 'd') {
                    int val = __builtin_va_arg(args, int);
                    itoa(val, num_buf, 10);
                } else if (format[fi] == 'u') {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    uitoa(val, num_buf, 10);
                } else {
                    unsigned int val = __builtin_va_arg(args, unsigned int);
                    uitoa(val, num_buf, 16);
                }
                int len = (int)strlen(num_buf);
                if (!left_align) {
                    while (len < min_width && bi < size - 1) {
                        buffer[bi++] = pad_zero ? '0' : ' ';
                        min_width--;
                    }
                }
                for (size_t k = 0; num_buf[k] && bi < size - 1; k++) {
                    buffer[bi++] = num_buf[k];
                }
                if (left_align) {
                    while (len < min_width && bi < size - 1) {
                        buffer[bi++] = ' ';
                        min_width--;
                    }
                }
            } else if (format[fi] == 's') {
                const char* s = __builtin_va_arg(args, const char*);
                if (!s) s = "(null)";
                int len = (int)strlen(s);
                if (!left_align) {
                    while (len < min_width && bi < size - 1) {
                        buffer[bi++] = ' ';
                        min_width--;
                    }
                }
                for (size_t k = 0; s[k] && bi < size - 1; k++) {
                    buffer[bi++] = s[k];
                }
                if (left_align) {
                    while (len < min_width && bi < size - 1) {
                        buffer[bi++] = ' ';
                        min_width--;
                    }
                }
            } else if (format[fi] == 'c') {
                char c = (char)__builtin_va_arg(args, int);
                buffer[bi++] = c;
            } else if (format[fi] == '%') {
                buffer[bi++] = '%';
            }
        } else {
            buffer[bi++] = format[fi];
        }
    }
    buffer[bi] = '\0';
    __builtin_va_end(args);
    return (int)bi;
}
