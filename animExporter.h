#ifndef GUARD_ANIM_EXPORTER_H
#define GUARD_ANIM_EXPORTER_H

typedef enum {
    UNKNOWN = 0,
    SA1     = 1,
    SA2     = 2,
    SA3     = 3,

    KATAM   = 10, // Kirby & the Amazing Mirror
} eGame;

typedef struct {
    RomPointer* data;
    s32 entryCount;
} AnimationTable;

typedef struct {
    u32 flags;
    StringId label;
    RomPointer address; // used for determining jump-destinations
    RomPointer jmpTarget; // only used by Jump-Command
    ACmd cmd;
} DynTableAnimCmd;

typedef struct {
    s32 offsetVariants;
    StringId name;
} DynTableAnim;

typedef struct {
    DynTableAnim* animations;
    u16* variantCounts;
} DynTable;

typedef struct {
    u32* base; // Always(?) points backwards
    s32* cursor;
    s32 subCount; // There can be multiple "sub animations" in one entry.
} AnimationData;

typedef struct {
    FILE* header;
    FILE* animTable;
} OutFiles;

typedef struct {
    char* strings;
    s32* offsets;
    u32 count;
} LabelStrings;

typedef struct {
    /* 0x00 void* */ RomPointer animations;
    /* 0x04 void* */ RomPointer dimensions;
    /* 0x08 u16** */ RomPointer oamData;
    /* 0x0C u16*  */ RomPointer palettes;
    /* 0x10 void* */ RomPointer tiles_4bpp;
    /* 0x14 void* */ RomPointer tiles_8bpp;
    /* 0x18 void* */ RomPointer sa3OnlyData;
} SpriteTablesROM;

typedef struct {
    bool wasInitialized;
    u16 tileCount;
    s32 tileIndex;

    s32 paletteId;
    u16 numColors;

    u16 animId;
    u16 variantId;
    u16 labelId;
} FrameData;

typedef struct {
    FrameData* data;
    u16 frameCount;
} FrameDataInput;

typedef struct  {
    /*0x00*/ u32 y : 8;
    /*0x01*/ u32 affineMode : 2;  // 0x1, 0x2 -> 0x4
    u32 objMode : 2;     // 0x4, 0x8 -> 0xC
    u32 mosaic : 1;      // 0x10
    u32 bpp : 1;         // 0x20
    u32 shape : 2;       // 0x40, 0x80 -> 0xC0

    /*0x02*/ u32 x : 9;
    u32 matrixNum : 5;   // bits 3/4 are h-flip/v-flip if not in affine mode
    u32 size : 2;        // 0x4000, 0x8000 -> 0xC000

    /*0x04*/ u16 tileNum : 10;    // 0x3FF
    u16 priority : 2;    // 0x400, 0x800 -> 0xC00
    u16 paletteNum : 4;
} OamSplit;

typedef struct {
    u8 flip;
    u8 oamIndex; // every animation has an oamData pointer, oamIndex starts at 0 for every new animation and ends at variantCount-1
    u16 numSubframes; // some sprite frames consist of multiple images (of the same size as GBA's Object Attribute Memory, e.g. 8x8, 8x32, 32x64, ...)

    u16 width;
    u16 height;
    s16 offsetX;
    s16 offsetY;
} SpriteOffset;
typedef struct {
    /* 0x00 */ RomPointer* animations;
    /* 0x04 */ RomPointer* dimensions; // -> SpriteOffset[numFramesOfAnimation]
    /* 0x08 */ RomPointer* oamData;    // -> oamSplit
    /* 0x0C */ u16*  palettes;
    /* 0x10 */ u8*   tiles_4bpp;
    /* 0x14 */ u8*   tiles_8bpp;
    /* 0x18 */ u8*   sa3OnlyData; // only in SA3 / KATAM
} SpriteTables;

#endif // GUARD_ANIM_EXPORTER_H