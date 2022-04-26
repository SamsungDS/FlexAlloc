/**
 * Assorted utility functions and error/debug macros
 *
 * Copyright (C) 2021 Joel Granados <j.granados@samsung.com>
 * Copyright (C) 2021 Jesper Devantier <j.devantier@samsung.com>
 *
 * @file flexalloc_util.h
 */
#ifndef __FLEXALLOC_UTIL_H
#define __FLEXALLOC_UTIL_H

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

#define FLA_ERR_MIN_NOPRINT 2001
#define FLA_ERR_PRX fprintf(stderr, "flexalloc ERR ");

#define FLA_ERR_PRINT(s)                      \
  FLA_ERR_PRX;                                \
  fprintf(stderr, s);                           \
  fflush(stderr);

#define FLA_ERR_PRINTF(f, ...) \
  FLA_ERR_PRX; \
  fprintf(stderr, f, __VA_ARGS__); \
  fflush(stderr);

#define FLA_ERRNO_VAL(e) e == 0 ? 0 : e > 0 ? -e : -1;

/**
 * @brief Activates error handling if condition is non zero.
 *
 * @param condition zero means no error
 * @param f The file name where the error occured
 * @param l The line where the error occurred
 * @param fmt The message to use in case there is an error
 * @param ... vriadic args for fmt. Can be empty.
 * @return condition is returned.
 */
static inline int
fla_err_fl(const int condition, const char * f, const int l, const char * fmt, ...)
{
  if(condition && condition < FLA_ERR_MIN_NOPRINT)
  {
    FLA_ERR_PRINTF(" %s(%d) ", f, l);
    va_list arglist;
    va_start(arglist, fmt);
    vfprintf(stderr, fmt, arglist);
    va_end(arglist);
    fprintf(stderr, "\n");
  }
  return condition;
}
#define FLA_ERR(condition, ...) fla_err_fl(condition, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Prints errno message if present.
 *
 * Activates error handling if condition is non zero. Will append errno string
 * to message if errno is set. Ingores errno if it is zero. We do not use errno to
 * detect an error, only to describe it once it has been detected.
 *
 * @param condition zero means no error
 * @param message The message to use in case there is an error
 * @param f The file name where the error occured
 * @param l The line where the error occurred
 * @return condition is returned when errno is 0, otherwise errno is returned
 */
static inline int
fla_err_errno_fl(const int condition, const char * message, const char * f,
                 const int l)
{
  // capture errno to avoid changing it when executing condition
  int __errno = FLA_ERRNO_VAL(errno);
  if(condition)
  {
    if(__errno)
    {
      FLA_ERR_PRINTF(" %s(%d) %s errno:%s\n", f, l, message, strerror(-__errno));
      return __errno;
    }
    else
    {
      FLA_ERR_PRINTF(" %s(%d) %s\n", f, l, message);
    }
  }
  return condition;
}
#define FLA_ERR_ERRNO(condition, message) fla_err_errno_fl(condition, message, __FILE__, __LINE__)

/**
 * Miscellaneous stuff
 */
#define fla_min(a, b) \
  ({ __typeof__ (a) _a = (a); \
      __typeof__ (b) _b = (b); \
    _a < _b ? _a : _b; })

#if FLA_VERBOSITY > 0
#define FLA_VBS_PRINTF(f, ...) fprintf(stderr, f, __VA_ARGS__);
#else
#define FLA_VBS_PRINTF(f, ...);
#endif

/**
 * Debugging messages
 */
#ifdef DEBUG
#define FLA_DBG_PRX fprintf(stderr, "flexalloc DBG ");

#define FLA_DBG_PRINT(s)                      \
  FLA_DBG_PRX;                                \
  fprintf(stderr, s);                           \
  fflush(stderr);

#define FLA_DBG_PRINTF(f, ...)                \
  FLA_DBG_PRX;                                \
  fprintf(stderr, f, __VA_ARGS__);              \
  fflush(stderr);

#define FLA_DBG_EXEC(statement) statement;
#else
#define FLA_DBG_PRX() ;
#define FLA_DBG_PRINT(s) ;
#define FLA_DBG_PRINTF(f, ...) ;
#define FLA_DBG_EXEC(statement) ;
#endif

/* Ceiling division provided:
 * - x and y are unsigned integers
 * - x is non-zero
 */
#define FLA_CEIL_DIV(x,y) (((x)+(y)-1)/(y))

/**
 * determine length of a fixed-size string.
 *
 * Returns the length of the string, defined as the number of leading
 * characters *before* the terminating null byte.
 * If the string has no null-terminating byte within the first maxlen bytes
 * strnlen() returns maxlen.
 *
 * @param s the string
 * @param maxlen maximum length of string
 * @return On success, length of string. On error, maxlen, meaning the string
 * is not null-terminated within the first maxlen bytes.
 */
size_t
strnlen(char *s, size_t maxlen);

/**
 * Create a duplicate of the provided string.
 *
 * Returns a pointer to a new string which is a duplicate of the provided string.
 * Memory is allocated by `malloc` and should be freed after use.
 */
char *
strdup(char const *s);

char *
strndup(char const *s, size_t const len);

#endif /* __FLEXALLOC_UTIL_H */
