// glibc compatibility for the Steam Runtime (sniper, glibc 2.31).
// The build host has a newer glibc, which makes the linker pick newer symbol
// versions. Each wrapper below is a hidden local definition, so every caller
// in the plugin (including prebuilt SDK archives) binds to it, and it forwards
// to the oldest version of the real function.
#if defined(__linux__)
#include <features.h>
#endif
// Only needed when the build host has a newer glibc than the Steam Runtime (2.31).
#if defined(__linux__) && defined(__x86_64__) && (__GLIBC__ > 2 || (__GLIBC__ == 2 && __GLIBC_MINOR__ > 31))
#define _LARGEFILE64_SOURCE 1
#include <dlfcn.h>
#include <pthread.h>
#include <sys/stat.h>

#define HIDDEN __attribute__((visibility("hidden")))
#define OLD(local, sym, ver) __asm__(".symver " #local "," #sym "@" ver)

extern double old_pow(double, double);           OLD(old_pow, pow, "GLIBC_2.2.5");
extern double old_fmod(double, double);          OLD(old_fmod, fmod, "GLIBC_2.2.5");
extern float old_fmodf(float, float);            OLD(old_fmodf, fmodf, "GLIBC_2.2.5");
extern void *old_dlopen(const char *, int);      OLD(old_dlopen, dlopen, "GLIBC_2.2.5");
extern int old_dlclose(void *);                  OLD(old_dlclose, dlclose, "GLIBC_2.2.5");
extern void *old_dlsym(void *, const char *);    OLD(old_dlsym, dlsym, "GLIBC_2.2.5");
extern int old_dlinfo(void *, int, void *);      OLD(old_dlinfo, dlinfo, "GLIBC_2.3.3");
extern int old_pthread_once(pthread_once_t *, void (*)(void));        OLD(old_pthread_once, pthread_once, "GLIBC_2.2.5");
extern int old_pthread_key_create(pthread_key_t *, void (*)(void *)); OLD(old_pthread_key_create, __pthread_key_create, "GLIBC_2.2.5");
extern int old_fxstat64(int, int, struct stat64 *); OLD(old_fxstat64, __fxstat64, "GLIBC_2.2.5");

HIDDEN double pow(double a, double b) { return old_pow(a, b); }
HIDDEN double fmod(double a, double b) { return old_fmod(a, b); }
HIDDEN float fmodf(float a, float b) { return old_fmodf(a, b); }
HIDDEN void *dlopen(const char *f, int m) { return old_dlopen(f, m); }
HIDDEN int dlclose(void *h) { return old_dlclose(h); }
HIDDEN void *dlsym(void *h, const char *s) { return old_dlsym(h, s); }
HIDDEN int dlinfo(void *h, int r, void *a) { return old_dlinfo(h, r, a); }
HIDDEN int pthread_once(pthread_once_t *o, void (*f)(void)) { return old_pthread_once(o, f); }
HIDDEN int __pthread_key_create(pthread_key_t *k, void (*d)(void *)) { return old_pthread_key_create(k, d); }
HIDDEN int fstat64(int fd, struct stat64 *buf) { return old_fxstat64(1, fd, buf); }

// glibc 2.38 headers redirect strto* to C23 variants. Bind the classic
// symbols by asm name so the headers can't redirect them back here.
extern long real_strtol(const char *, char **, int) __asm__("strtol");
extern long long real_strtoll(const char *, char **, int) __asm__("strtoll");
extern unsigned long real_strtoul(const char *, char **, int) __asm__("strtoul");
extern unsigned long long real_strtoull(const char *, char **, int) __asm__("strtoull");
HIDDEN long __isoc23_strtol(const char *s, char **e, int b) { return real_strtol(s, e, b); }
HIDDEN long long __isoc23_strtoll(const char *s, char **e, int b) { return real_strtoll(s, e, b); }
HIDDEN unsigned long __isoc23_strtoul(const char *s, char **e, int b) { return real_strtoul(s, e, b); }
HIDDEN unsigned long long __isoc23_strtoull(const char *s, char **e, int b) { return real_strtoull(s, e, b); }

// glibc 2.32 libstdc++ fast path; 0 = always use atomics (correct, slightly slower).
HIDDEN char __libc_single_threaded = 0;
#endif
