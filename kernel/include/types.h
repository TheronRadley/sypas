/* SYPAS kernel — basic fixed-width types. Freestanding C17. */
#ifndef SYPAS_TYPES_H
#define SYPAS_TYPES_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdarg.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t   i8;
typedef int16_t  i16;
typedef int32_t  i32;
typedef int64_t  i64;

typedef u64 paddr_t;   /* physical address */
typedef u64 vaddr_t;   /* virtual address  */

#define PAGE_SIZE   4096UL
#define PAGE_SHIFT  12

#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((u64)(a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((u64)(a) - 1))
#define ARRAY_LEN(a)     (sizeof(a) / sizeof((a)[0]))

#endif
