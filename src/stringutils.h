// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_STR_UTILS_H
# define _HEADER_STR_UTILS_H

#define str_equal(a, b) (!strcmp((a), (b)))

int str_startswith(const char *head, const char *worm);

int str_endswith(const char *tail, const char *tiger);

#define HAVE_VA_COPY 1

#ifdef HAVE_VA_COPY
  /* C99: use va_copy(), and match it with calls to va_end(). */
  #define VA_COPY(dest, src)      va_copy(dest, src)
  #define VA_COPY_END(dest)       va_end(dest)
#elif defined(HAVE_UNDERSCORE_UNDERSCORE_VA_COPY)
  /* Earlier drafts of the C99 standard used __va_copy(). */
  #define VA_COPY(dest, src)      __va_copy(dest, src)
  #define VA_COPY_END(dest)       va_end(dest)
#else
  /* Pre-C99: the best we can do is to assume that va_list
     values can be freely copied.  This works on most (but
     not all) pre-C99 C implementations. */
  #define VA_COPY(dest, src)      ((dest) = (src), (void) 0)
  #define VA_COPY_END(dest)       ((void) 0)
#endif

#ifdef HAVE_LONG_DOUBLE
#define LDOUBLE long double
#else
#define LDOUBLE double
#endif

#ifdef HAVE_LONG_LONG
#define LLONG long long
#else
#define LLONG long
#endif


/* format read states */
#define DP_S_DEFAULT 0
#define DP_S_FLAGS   1
#define DP_S_MIN     2
#define DP_S_DOT     3
#define DP_S_MAX     4
#define DP_S_MOD     5
#define DP_S_CONV    6
#define DP_S_DONE    7

/* format flags - Bits */
#define DP_F_MINUS     (1 << 0)
#define DP_F_PLUS      (1 << 1)
#define DP_F_SPACE     (1 << 2)
#define DP_F_NUM       (1 << 3)
#define DP_F_ZERO      (1 << 4)
#define DP_F_UP        (1 << 5)
#define DP_F_UNSIGNED     (1 << 6)

/* Conversion Flags */
#define DP_C_SHORT   1
#define DP_C_LONG    2
#define DP_C_LDOUBLE 3
#define DP_C_LLONG   4

#define char_to_int(p) ((p)- '0')
#ifndef MAX
#define MAX(p,q) (((p) >= (q)) ? (p) : (q))
#endif



/* Define to 1 if you have the `vsnprintf' function. */
#define HAVE_VSNPRINTF 1

/* yes this really must be a ||. Don't muck with this (tridge) */
#if !defined(HAVE_VSNPRINTF) || !defined(HAVE_C99_VSNPRINTF)
int vsnprintf (char *str, size_t count, const char *fmt, va_list args);
int vasprintf(char **ptr, const char *format, va_list ap);
int asprintf(char **ptr, const char *format, ...);
#endif //HAVE_VSPRINTF


#ifndef HAVE_STRLCPY
size_t strlcpy(char *d, const char *s, size_t bufsize);
#endif

#endif //_HEADER_STR_UTILS_H
