#ifndef GUARD_ANIMATION_COMMANDS_H
#define GUARD_ANIMATION_COMMANDS_H

#define AnimCommandSizeInWords(_structType) ((sizeof(_structType)) / sizeof(s32))

typedef u16 AnimId;

typedef struct {
    s32 index;
    s8 left;
    s8 top;
    s8 right;
    s8 bottom;
} Hitbox;

typedef struct {
    /* 0x00 */ s32 cmdId; // -2

    // Note(Jace): This needs to be signed, since a
    //             negative value infers that it's using 8bit-colors
    /* 0x04 */ s32 tileIndex;

    /* 0x08 */ u32 numTilesToCopy;
} ACmd_GetTiles;

typedef struct {
    /* 0x00 */ s32 cmdId; // -2

    /* 0x04 */ s32 palId;
    /* 0x06 */ u16 numColors;
    /* 0x08 */ u16 insertOffset;
} ACmd_GetPalette;

typedef struct {
    /* 0x00 */ s32 cmdId; // -3

    /* 0x04 */ s32 offset;
} ACmd_JumpBack;

typedef struct {
    /* 0x00 */ s32 cmdId; // -3

    /* 0x04 */ s32 offset;

    // This is a custom element for the editor
    // used for creating an offset, instead of just
    // printing the hardcoded value.
    // cast as 'DynTableAnimCmd'!
    void* jumpTarget; 
} ExCmd_JumpBack;

typedef struct {
    /* 0x00 */ s32 cmdId; // -4
} ACmd_End;

typedef struct {
    /* 0x00 */ s32 cmdId; // -5

    /* 0x04 */ u16 songId;
} ACmd_PlaySoundEffect;

// TODO: param types unknown
typedef struct {
    /* 0x00 */ s32 cmdId; // -6

    /* 0x04 */ Hitbox hitbox;
} ACmd_AddHitbox;

typedef struct {
    /* 0x00 */ s32 cmdId; // -7

    /* 0x04 */ u16 x;
    /* 0x06 */ u16 y;
} ACmd_TranslateSprite;

typedef struct {
    /* 0x00 */ s32 cmdId; // -8

    /* 0x04 */ s32 unk4;
    /* 0x08 */ s32 unk8;
} ACmd_8;

typedef struct {
    /* 0x00 */ s32 cmdId; // -9

    /* 0x04 */ AnimId animId;
    /* 0x08 */ u16 variant;
} ACmd_SetIdAndVariant;

typedef struct {
    /* 0x00 */ s32 cmdId; // -10

    /* 0x04 */ s32 unk4;
    /* 0x08 */ s32 unk8;
    /* 0x0C */ s32 unkC;
} ACmd_10;

typedef struct {
    /* 0x00 */ s32 cmdId; // -11

    /* 0x04 */ s32 unk4; // the logic of animCmd_11 suggests that only values of 0-3 makes
                         // sense here.
} ACmd_SetSpritePriority;

typedef struct {
    /* 0x00 */ s32 cmdId; // -12

    /* 0x04 */ s32 unk4;
} ACmd_12;


// Only for use with exporter, not in-game!
typedef struct {
    union {
        // "cmdId" has to be greater than 0 for this command,
        // since it's actually not a command.
        s32 cmdId;

        // number of frames this will be displayed
        s32 displayForNFrames;
    };

    // frameId of this animation that should be displayed
    s32 frameIndex;     
} ACmd_Display;

// Only for use with exporter, not in-game!
typedef union {
    s32 id;

    ACmd_GetTiles _tiles;
    ACmd_GetPalette _pal;
    ACmd_JumpBack _jump;
    ACmd_End _end;
    ACmd_PlaySoundEffect _sfx;
    ACmd_AddHitbox _hitbox;
    ACmd_TranslateSprite _translate;
    ACmd_8 _8;
    ACmd_SetIdAndVariant _animId;
    ACmd_10 _10;
    ACmd_SetSpritePriority _prio;
    ACmd_12 _12;

    // Not a regular "AnimCmd"
    ACmd_Display _display;

    // Editor-only!
    ExCmd_JumpBack _exJump;
} ACmd;

/* Flags are only for use with exporter, not in-game! */
#define ACMD_FLAG__IS_START_OF_ANIM 0x1
#define ACMD_FLAG__IS_POINTED_TO  0x2
#define ACMD_FLAG__NEEDS_LABEL    ACMD_FLAG__IS_POINTED_TO

#endif // GUARD_ANIMATION_COMMANDS_H