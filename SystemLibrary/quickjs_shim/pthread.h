#ifndef QJS_SHIM_PTHREAD_H
#define QJS_SHIM_PTHREAD_H

typedef int pthread_t;
typedef int pthread_mutex_t;
typedef int pthread_mutexattr_t;
typedef int pthread_cond_t;
typedef int pthread_condattr_t;
typedef int pthread_once_t;

#define PTHREAD_MUTEX_INITIALIZER 0
#define PTHREAD_COND_INITIALIZER  0
#define PTHREAD_ONCE_INIT         0

static inline int pthread_mutex_init(pthread_mutex_t *m, const pthread_mutexattr_t *a) { (void)a; if (m) *m = 0; return 0; }
static inline int pthread_mutex_destroy(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_lock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_mutex_unlock(pthread_mutex_t *m) { (void)m; return 0; }
static inline int pthread_cond_init(pthread_cond_t *c, const pthread_condattr_t *a) { (void)a; if (c) *c = 0; return 0; }
static inline int pthread_cond_destroy(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_signal(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_broadcast(pthread_cond_t *c) { (void)c; return 0; }
static inline int pthread_cond_wait(pthread_cond_t *c, pthread_mutex_t *m) { (void)c; (void)m; return 0; }
static inline int pthread_cond_timedwait(pthread_cond_t *c, pthread_mutex_t *m, const void *ts) { (void)c; (void)m; (void)ts; return 0; }
static inline int pthread_condattr_init(pthread_condattr_t *a) { (void)a; return 0; }
static inline int pthread_condattr_setclock(pthread_condattr_t *a, int clk) { (void)a; (void)clk; return 0; }
static inline int pthread_condattr_destroy(pthread_condattr_t *a) { (void)a; return 0; }
static inline int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
    if (once_control && *once_control == 0) {
        *once_control = 1;
        if (init_routine) init_routine();
    }
    return 0;
}

#endif
