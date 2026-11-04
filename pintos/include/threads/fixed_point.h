#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H

#include <stdint.h>

/* 스케일 팩터 fixed */
#define F (1 << 14)

/* fixed type */
typedef int fixed_t;

/* return */
static inline fixed_t int_to_fp       (int n)                   {return n * F;}
static inline int     fp_to_int       (fixed_t x)               {return x / F;}
static inline int     fp_to_int_round (fixed_t x)               {return x < 0? (x - F/2) / F : (x + F/2) / F;}

/* add, sub */
static inline fixed_t fp_add          (fixed_t x, fixed_t y)    {return x + y;}
static inline fixed_t fp_sub          (fixed_t x, fixed_t y)    {return x - y;}
static inline fixed_t fp_add_int      (fixed_t x, int n)        {return x + n * F;}
static inline fixed_t fp_sub_int      (fixed_t x, int n)        {return x - n * F;}

/* mul div */
static inline fixed_t fp_mul          (fixed_t x, fixed_t y)    {return ((int64_t) x) * y / F;}
static inline fixed_t fp_div          (fixed_t x, fixed_t y)    {return ((int64_t) x) * F / y;}
static inline fixed_t fp_mul_int      (fixed_t x, int n)        {return x * n;}
static inline fixed_t fp_div_int      (fixed_t x, int n)        {return x / n;}

#endif /* threads/fixed_point.h */