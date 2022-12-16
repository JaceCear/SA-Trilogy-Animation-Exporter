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
} SpriteTablesROM;

typedef struct {
    /* 0x00 */ void* animations;
    /* 0x04 */ void* dimensions;
    /* 0x08 */ u16** oamData;
    /* 0x0C */ u16*  palettes;
    /* 0x10 */ u8*   tiles_4bpp;
    /* 0x14 */ u8*   tiles_8bpp;
} SpriteTables;

#endif // GUARD_ANIM_EXPORTER_H