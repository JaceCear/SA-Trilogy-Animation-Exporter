#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "ArenaAlloc.h"

#define ARENA_SIZE (2*1024*1024)

static void memArenaExpand(MemArena *arena, s32 numNewArenas);

void
memArenaInit(MemArena *arena) {
    long long size = ARENA_SIZE;
    size++; // @NOTE: what's this size++ here for?
    int sizeofLong = sizeof(long long);
    arena->memory = calloc(1, size);
    
    assert(arena->memory);
    
    arena->size = ARENA_SIZE;
    arena->offset = 0;
    
}

void
memArenaFree(MemArena *arena) {
    free(arena->memory);

    memset(arena, 0, sizeof(MemArena));
}

// Reserve 'byteCount' amount of memory and set it to zero.
void*
memArenaReserve(MemArena *arena, u64 byteCount) {
    if (byteCount == 0)
        return NULL;

    ALIGN(arena->offset, 4);
    
    if(arena->size < (arena->offset + byteCount)) {
        // TODO: There's a bug that stems from realloc returning a different address after calling it.
        //       Use VirtualAlloc and the Unix-equivalent to fix that.
        //       That's why we're asserting here.
        // Allocate more memory upfront for now, if you reach this assert!
        assert(FALSE);

        int newArenaCount = ((arena->offset + byteCount) - arena->size);
        newArenaCount /= ARENA_SIZE;
        newArenaCount += 1;
        
        memArenaExpand(arena, newArenaCount);
    }

    u8* memory = ((u8*)arena->memory + arena->offset);
    memset(memory, 0, byteCount);

    arena->offset += byteCount;

    return memory;
}

void*
memArenaAddMemory(MemArena *arena, void *source, u64 srcLength) {
    u8* dest = memArenaReserve(arena, srcLength);

    memcpy(dest, source, srcLength);
    
    return dest;
}

char*
memArenaAddString(MemArena *arena, char *source) {
    int length = strlen(source) + 1;
    
    char *str = (char*)memArenaAddMemory(arena, source, length);
    str[length-1] = '\0';
    
    return str;
}

u64*
memArenaAddU64(MemArena *arena, u64 number) {
    ALIGN(arena->offset, 8);
    u64* targetMem;
    
    if(arena->size < arena->offset + sizeof(number)) {
        memArenaExpand(arena, 1);
    }
    
    targetMem = (u64*)((u8*)arena->memory + arena->offset);
    *targetMem = number;
    arena->offset += sizeof(number);
    
    return targetMem;
}

u32*
memArenaAddU32(MemArena *arena, u32 number) {
    ALIGN(arena->offset, 4);
    u32* targetMem;
    
    if(arena->size < arena->offset + sizeof(number)) {
        memArenaExpand(arena, 1);
    }
    
    targetMem = (u32*)((u8*)arena->memory + arena->offset);
    *targetMem = number;
    arena->offset += sizeof(number);
    
    return targetMem;
}

u16*
memArenaAddU16(MemArena *arena, u16 number) {
    ALIGN(arena->offset, 2);
    u16* targetMem;
    
    if(arena->size < arena->offset + sizeof(number)) {
        memArenaExpand(arena, 1);
    }
    
    targetMem = (u16*)((u8*)arena->memory + arena->offset);
    *targetMem = number;
    arena->offset += sizeof(number);
    
    return targetMem;
}

u8*
memArenaAddU8(MemArena *arena, u8 number) {
    u8* targetMem;
    
    if(arena->size < arena->offset + sizeof(number)) {
        memArenaExpand(arena, 1);
    }
    
    targetMem = ((u8*)arena->memory + arena->offset);
    *targetMem = number;
    arena->offset += sizeof(number);
    
    return targetMem;
}

static void
memArenaExpand(MemArena *arena, s32 numNewArenas) {
    long long newSize;
    
    if(numNewArenas <= 0) {
        numNewArenas = 1;
    }
    
    newSize = arena->size + ARENA_SIZE*numNewArenas;
    
    // Check for overflow
    assert(newSize > 0);
    
    // @BUG @SPEED
    // malloc only takes an int, use VirtualAlloc() instead...
    arena->memory = realloc(arena->memory, newSize);
    if(arena->memory) {
        arena->size = newSize;
    } else {
        arena->size = 0;
    }
}
