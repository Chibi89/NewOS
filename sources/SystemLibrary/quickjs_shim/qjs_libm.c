#include <math.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdarg.h>

// ============================================================================
// Implementazione freestanding math.h (SSE2 / IEEE 754) per QuickJS
// ============================================================================
double fabs(double x) { return x < 0.0 ? -x : x; }
float fabsf(float x) { return x < 0.0f ? -x : x; }

double copysign(double x, double y) {
    union { double d; uint64_t u; } ux = { x }, uy = { y };
    ux.u = (ux.u & 0x7FFFFFFFFFFFFFFFULL) | (uy.u & 0x8000000000000000ULL);
    return ux.d;
}

double trunc(double x) {
    if (isnan(x) || isinf(x) || fabs(x) >= 4503599627370496.0) return x;
    return (double)(int64_t)x;
}

double floor(double x) {
    double t = trunc(x);
    return (t > x) ? (t - 1.0) : t;
}

double ceil(double x) {
    double t = trunc(x);
    return (t < x) ? (t + 1.0) : t;
}

double round(double x) {
    return (x >= 0.0) ? floor(x + 0.5) : ceil(x - 0.5);
}

double rint(double x) {
    if (isnan(x) || isinf(x) || fabs(x) >= 4503599627370496.0) return x;
    double r = round(x);
    if (fabs(x - r) == 0.5) {
        int64_t ir = (int64_t)r;
        if (ir & 1) r += (x > r ? 1.0 : -1.0);
    }
    return r;
}

double nearbyint(double x) { return rint(x); }
long lrint(double x) { return (long)rint(x); }
long long llrint(double x) { return (long long)rint(x); }

double fmod(double x, double y) {
    if (isnan(x) || isnan(y) || isinf(x) || y == 0.0) return NAN;
    if (isinf(y)) return x;
    double q = trunc(x / y);
    return x - q * y;
}

double remainder(double x, double y) {
    if (isnan(x) || isnan(y) || isinf(x) || y == 0.0) return NAN;
    double q = rint(x / y);
    return x - q * y;
}

double sqrt(double x) {
    if (x < 0.0) return NAN;
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    double r;
    __asm__ volatile("sqrtsd %1, %0" : "=x"(r) : "x"(x));
    return r;
}

double fmin(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x < y) ? x : y;
}

double fmax(double x, double y) {
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    return (x > y) ? x : y;
}

double ldexp(double x, int exp) {
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    while (exp > 0) { x *= 2.0; exp--; if (isinf(x)) break; }
    while (exp < 0) { x *= 0.5; exp++; if (x == 0.0) break; }
    return x;
}
double scalbn(double x, int exp) { return ldexp(x, exp); }

double frexp(double x, int *exp) {
    if (!exp) return x;
    *exp = 0;
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    double ax = fabs(x);
    int e = 0;
    while (ax >= 1.0) { ax *= 0.5; e++; }
    while (ax < 0.5) { ax *= 2.0; e--; }
    *exp = e;
    return (x < 0.0) ? -ax : ax;
}

double modf(double x, double *iptr) {
    double i = trunc(x);
    if (iptr) *iptr = i;
    return x - i;
}

double exp(double x) {
    if (isnan(x)) return NAN;
    if (x > 709.78) return INFINITY;
    if (x < -745.13) return 0.0;
    int k = (int)round(x / M_LN2);
    double r = x - k * M_LN2;
    double term = 1.0, sum = 1.0;
    for (int i = 1; i <= 18; i++) {
        term *= r / (double)i;
        sum += term;
    }
    return ldexp(sum, k);
}

double exp2(double x) { return exp(x * M_LN2); }
double expm1(double x) {
    if (fabs(x) < 1e-5) return x + 0.5 * x * x + (x * x * x) / 6.0;
    return exp(x) - 1.0;
}

double log(double x) {
    if (isnan(x) || x < 0.0) return NAN;
    if (x == 0.0) return -INFINITY;
    if (isinf(x)) return INFINITY;
    int k = 0;
    double m = frexp(x, &k);
    if (m < M_SQRT1_2) { m *= 2.0; k--; }
    double y = (m - 1.0) / (m + 1.0);
    double y2 = y * y;
    double sum = 0.0, p = y;
    for (int i = 1; i <= 21; i += 2) {
        sum += p / (double)i;
        p *= y2;
    }
    return 2.0 * sum + (double)k * M_LN2;
}

double log2(double x) { return log(x) * M_LOG2E; }
double log10(double x) { return log(x) * M_LOG10E; }
double log1p(double x) {
    if (fabs(x) < 1e-5) return x - 0.5 * x * x + (x * x * x) / 3.0;
    return log(1.0 + x);
}

double pow(double x, double y) {
    if (y == 0.0) return 1.0;
    if (isnan(x) || isnan(y)) return NAN;
    if (x == 1.0) return 1.0;
    if (x == 0.0) return (y > 0.0) ? 0.0 : INFINITY;
    if (x < 0.0) {
        if (floor(y) != y) return NAN;
        double r = exp(y * log(-x));
        return (fmod(fabs(y), 2.0) == 1.0) ? -r : r;
    }
    return exp(y * log(x));
}

double cbrt(double x) {
    if (x == 0.0 || isnan(x) || isinf(x)) return x;
    return (x < 0.0) ? -pow(-x, 1.0 / 3.0) : pow(x, 1.0 / 3.0);
}

double hypot(double x, double y) {
    if (isinf(x) || isinf(y)) return INFINITY;
    if (isnan(x) || isnan(y)) return NAN;
    return sqrt(x * x + y * y);
}

static double reduce_pi(double x, int *quad) {
    double k = round(x / (M_PI * 0.5));
    *quad = ((int)(int64_t)k) & 3;
    return x - k * (M_PI * 0.5);
}

static double sin_core(double r) {
    double r2 = r * r, term = r, sum = r;
    for (int i = 1; i <= 9; i++) {
        term *= -r2 / (double)((2 * i) * (2 * i + 1));
        sum += term;
    }
    return sum;
}

static double cos_core(double r) {
    double r2 = r * r, term = 1.0, sum = 1.0;
    for (int i = 1; i <= 9; i++) {
        term *= -r2 / (double)((2 * i - 1) * (2 * i));
        sum += term;
    }
    return sum;
}

double sin(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    int q = 0;
    double r = reduce_pi(x, &q);
    switch (q) {
        case 0: return sin_core(r);
        case 1: return cos_core(r);
        case 2: return -sin_core(r);
        default: return -cos_core(r);
    }
}

double cos(double x) {
    if (isnan(x) || isinf(x)) return NAN;
    int q = 0;
    double r = reduce_pi(x, &q);
    switch (q) {
        case 0: return cos_core(r);
        case 1: return -sin_core(r);
        case 2: return -cos_core(r);
        default: return sin_core(r);
    }
}

double tan(double x) { return sin(x) / cos(x); }

double atan(double x) {
    if (isnan(x)) return NAN;
    if (isinf(x)) return (x > 0.0) ? (M_PI * 0.5) : (-M_PI * 0.5);
    int neg = 0, inv = 0;
    if (x < 0.0) { neg = 1; x = -x; }
    if (x > 1.0) { inv = 1; x = 1.0 / x; }
    double r = x / (1.0 + sqrt(1.0 + x * x));
    double r2 = r * r, p = r, sum = 0.0;
    for (int i = 0; i < 15; i++) {
        sum += ((i & 1) ? -p : p) / (double)(2 * i + 1);
        p *= r2;
    }
    double res = 2.0 * sum;
    if (inv) res = M_PI * 0.5 - res;
    return neg ? -res : res;
}

double atan2(double y, double x) {
    if (isnan(x) || isnan(y)) return NAN;
    if (x > 0.0) return atan(y / x);
    if (x < 0.0) return (y >= 0.0) ? (atan(y / x) + M_PI) : (atan(y / x) - M_PI);
    if (y > 0.0) return M_PI * 0.5;
    if (y < 0.0) return -M_PI * 0.5;
    return 0.0;
}

double asin(double x) {
    if (isnan(x) || fabs(x) > 1.0) return NAN;
    if (x == 1.0) return M_PI * 0.5;
    if (x == -1.0) return -M_PI * 0.5;
    return atan2(x, sqrt(1.0 - x * x));
}

double acos(double x) { return M_PI * 0.5 - asin(x); }
double sinh(double x) { return 0.5 * (exp(x) - exp(-x)); }
double cosh(double x) { return 0.5 * (exp(x) + exp(-x)); }
double tanh(double x) {
    if (x > 20.0) return 1.0;
    if (x < -20.0) return -1.0;
    double e2 = exp(2.0 * x);
    return (e2 - 1.0) / (e2 + 1.0);
}
double asinh(double x) { return log(x + sqrt(x * x + 1.0)); }
double acosh(double x) { return (x < 1.0) ? NAN : log(x + sqrt(x * x - 1.0)); }
double atanh(double x) { return (fabs(x) >= 1.0) ? ((x == 1.0) ? INFINITY : ((x == -1.0) ? -INFINITY : NAN)) : 0.5 * log((1.0 + x) / (1.0 - x)); }

// ============================================================================
// Time / Stdio / Stdlib shims per QuickJS
// ============================================================================
static FILE g_dummy_file = {0};
FILE *stdout = &g_dummy_file;
FILE *stderr = &g_dummy_file;
FILE *stdin  = &g_dummy_file;

static uint64_t g_qjs_ticks = 1731960000ULL;

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
    (void)clk_id;
    if (tp) {
        g_qjs_ticks++;
        tp->tv_sec = (time_t)g_qjs_ticks;
        tp->tv_nsec = 0;
    }
    return 0;
}

int gettimeofday(struct timeval *tv, struct timezone *tz) {
    (void)tz;
    if (tv) {
        g_qjs_ticks++;
        tv->tv_sec = (time_t)g_qjs_ticks;
        tv->tv_usec = 0;
    }
    return 0;
}

time_t time(time_t *tloc) {
    g_qjs_ticks++;
    if (tloc) *tloc = (time_t)g_qjs_ticks;
    return (time_t)g_qjs_ticks;
}

struct tm *gmtime_r(const time_t *timep, struct tm *result) {
    (void)timep;
    if (result) {
        for (size_t i = 0; i < sizeof(*result); i++) ((uint8_t *)result)[i] = 0;
        result->tm_year = 126;
        result->tm_mon = 10;
        result->tm_mday = 18;
    }
    return result;
}

struct tm *localtime_r(const time_t *timep, struct tm *result) {
    return gmtime_r(timep, result);
}

time_t mktime(struct tm *tm) {
    (void)tm;
    return (time_t)g_qjs_ticks;
}

double difftime(time_t time1, time_t time0) {
    return (double)(time1 - time0);
}

int abs(int j) { return j < 0 ? -j : j; }
long labs(long j) { return j < 0 ? -j : j; }
long long llabs(long long j) { return j < 0 ? -j : j; }
static void qjs_ser(const char *s) {
    while (s && *s) {
        unsigned char al = (unsigned char)*s++;
        __asm__ volatile("outb %0, %1" :: "a"(al), "Nd"((unsigned short)0x3F8));
    }
}
void abort(void) { qjs_ser("[QJS ABORT]\n"); while (1) {} }
void exit(int status) { (void)status; qjs_ser("[QJS EXIT]\n"); while (1) {} }
char *getenv(const char *name) { (void)name; return NULL; }
int fflush(FILE *stream) { (void)stream; return 0; }
int fputc(int c, FILE *stream) { (void)c; (void)stream; return c; }
int fputs(const char *s, FILE *stream) { (void)stream; qjs_ser(s); return 0; }
int putchar(int c) { return c; }
int puts(const char *s) { (void)s; return 0; }
size_t fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    (void)ptr; (void)stream;
    return size * nmemb;
}
int fprintf(FILE *stream, const char *format, ...) { (void)stream; (void)format; return 0; }
int vfprintf(FILE *stream, const char *format, va_list ap) { (void)stream; (void)format; (void)ap; return 0; }
int printf(const char *format, ...) { (void)format; return 0; }

int sscanf(const char *str, const char *format, ...) {
    (void)str; (void)format;
    return 0;
}

double strtod(const char *nptr, char **endptr) {
    const char *s = nptr;
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r') s++;
    int sign = 1;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    double val = 0.0;
    int any = 0;
    while (*s >= '0' && *s <= '9') {
        val = val * 10.0 + (*s - '0');
        s++;
        any = 1;
    }
    if (*s == '.') {
        s++;
        double frac = 0.1;
        while (*s >= '0' && *s <= '9') {
            val += (*s - '0') * frac;
            frac *= 0.1;
            s++;
            any = 1;
        }
    }
    if (any && (*s == 'e' || *s == 'E')) {
        s++;
        int esign = 1, expv = 0;
        if (*s == '-') { esign = -1; s++; }
        else if (*s == '+') s++;
        while (*s >= '0' && *s <= '9') {
            expv = expv * 10 + (*s - '0');
            s++;
        }
        val *= pow(10.0, (double)(esign * expv));
    }
    if (endptr) *endptr = (char *)(any ? s : nptr);
    return sign * val;
}

unsigned __int128 __udivti3(unsigned __int128 n, unsigned __int128 d) {
    if (d == 0) return 0;
    if (n < d) return 0;
    unsigned __int128 q = 0, r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) {
            r -= d;
            q |= ((unsigned __int128)1 << i);
        }
    }
    return q;
}

unsigned __int128 __umodti3(unsigned __int128 n, unsigned __int128 d) {
    if (d == 0) return 0;
    if (n < d) return n;
    unsigned __int128 r = 0;
    for (int i = 127; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1);
        if (r >= d) r -= d;
    }
    return r;
}
