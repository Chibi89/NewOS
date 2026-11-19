#ifndef NS_SHIM_ERRNO_H
#define NS_SHIM_ERRNO_H
extern int errno;
#define EINVAL 22
#define ENOMEM 12
#define EILSEQ 84
#define E2BIG  7
#ifndef ETIMEDOUT
#define ETIMEDOUT 110
#endif
#endif
