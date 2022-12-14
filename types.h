#ifndef GUARD_TYPES_H
#define GUARD_TYPES_H

#define FALSE 0
#define TRUE  1

typedef signed int bool;

typedef signed char         s8;
typedef signed short       s16;
typedef signed int         s32;
typedef signed long long   s64;

typedef unsigned char       u8;
typedef unsigned short     u16;
typedef unsigned int       u32;
typedef unsigned long long u64;

typedef u32 RomPointer;
typedef u32 StringId;

#ifndef ROM_BASE
#define ROM_BASE 0x08000000
#endif

#define Min(a,b) (((a) < (b)) ? (a) : (b))
#define Max(a,b) (((a) > (b)) ? (a) : (b))

#define ALIGN(num, align) (num = ((num + (align - 1)) & -align))

#define DEBUG 1

#ifdef DEBUG
#define assert(cond)                                                    \
if (!(cond)) {                                                                    \
    fprintf(stderr, "ASSERT failed - %s:%u\n", __FILE__, __LINE__); \
    *(int*)0 = 0x0BADC0DE;                                                        \
}
#else
#define assert(cond)
#endif

#endif // GUARD_TYPES_H