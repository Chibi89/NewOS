#ifndef QJS_SHIM_MATH_H
#define QJS_SHIM_MATH_H

#define NAN       (__builtin_nanf(""))
#define INFINITY  (__builtin_inff())
#define HUGE_VAL  (__builtin_huge_val())
#define M_PI      3.14159265358979323846
#define M_E       2.71828182845904523536
#define M_LN2     0.69314718055994530942
#define M_LN10    2.30258509299404568402
#define M_LOG2E   1.44269504088896340736
#define M_LOG10E  0.43429448190325182765
#define M_SQRT2   1.41421356237309504880
#define M_SQRT1_2 0.70710678118654752440

#define isnan(x)    __builtin_isnan(x)
#define isinf(x)    __builtin_isinf(x)
#define isfinite(x) __builtin_isfinite(x)
#define signbit(x)  __builtin_signbit(x)

#define FP_NAN       0
#define FP_INFINITE  1
#define FP_ZERO      2
#define FP_SUBNORMAL 3
#define FP_NORMAL    4
#define fpclassify(x) __builtin_fpclassify(FP_NAN, FP_INFINITE, FP_NORMAL, FP_SUBNORMAL, FP_ZERO, x)

double fabs(double x);
float  fabsf(float x);
double floor(double x);
double ceil(double x);
double trunc(double x);
double round(double x);
double rint(double x);
double nearbyint(double x);
double fmod(double x, double y);
double remainder(double x, double y);
double sqrt(double x);
double cbrt(double x);
double hypot(double x, double y);
double sin(double x);
double cos(double x);
double tan(double x);
double asin(double x);
double acos(double x);
double atan(double x);
double atan2(double y, double x);
double sinh(double x);
double cosh(double x);
double tanh(double x);
double asinh(double x);
double acosh(double x);
double atanh(double x);
double exp(double x);
double exp2(double x);
double expm1(double x);
double log(double x);
double log2(double x);
double log10(double x);
double log1p(double x);
double pow(double x, double y);
double ldexp(double x, int exp);
double scalbn(double x, int exp);
double frexp(double x, int *exp);
double modf(double x, double *iptr);
double copysign(double x, double y);
double fmin(double x, double y);
double fmax(double x, double y);
long lrint(double x);
long long llrint(double x);

#endif
