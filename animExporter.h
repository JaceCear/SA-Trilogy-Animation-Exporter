#ifndef GUARD_ANIM_EXPORTER_H
#define GUARD_ANIM_EXPORTER_H

#define GAME_SA1            1
#define GAME_SA2            2
#define GAME_SA3            3
#define GAME_KATAM          10 // Kirby & the Amazing Mirror

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


#endif // GUARD_ANIM_EXPORTER_H