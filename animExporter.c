#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef _MSC_VER
#include <direct.h>
#define mkdir _mkdir
#else
#include <sys/stat.h>
#define mkdir(a) { mode_t perms = 666; mkdir((a), perms); }
#endif
#include <math.h> // for sqrtf


#include "types.h"
#include "arenaAlloc.h"

// Needs to be included before animExporter.h
#include "animation_commands.h"

#include "animExporter.h"

#define SizeofArray(array) ((sizeof(array)) / (sizeof(array[0])))

#define OffsetPointer(ptrToOffset) (((u8*)(ptrToOffset)) + *(ptrToOffset))


// TODO(Jace): Allow for custom animation table size
#define SA1_ANIMATION_COUNT    908
#define SA2_ANIMATION_COUNT    1133
#define SA3_ANIMATION_COUNT    1524

#define KATAM_ANIMATION_COUNT  939

const u32 g_TotalAnimationCount[] = {
    /*  1 */ [SA1] = SA1_ANIMATION_COUNT,
    /*  2 */ [SA2] = SA2_ANIMATION_COUNT,
    /*  3 */ [SA3] = SA3_ANIMATION_COUNT,

    /* 10 */ [KATAM] = KATAM_ANIMATION_COUNT,
};

// Creating separate files for each animation takes a lot of time (4 seconds for me),
// so print to stdout per default, which is instant if it's directed to a file with ' > file.out'
#define PRINT_TO_STDOUT FALSE

// Identifiers
#define AnimCmd_GetTiles        -1
#define AnimCmd_GetPalette      -2
#define AnimCmd_JumpBack        -3
#define AnimCmd_4               -4
#define AnimCmd_PlaySoundEffect -5
#define AnimCmd_6               -6
#define AnimCmd_TranslateSprite -7
#define AnimCmd_8               -8
#define AnimCmd_SetIdAndVariant -9
#define AnimCmd_10              -10
#define AnimCmd_11              -11
#define AnimCmd_12              -12
#define AnimCmd_DisplayFrame    (AnimCmd_12-1)

const char* animCommands[] = {
    "AnimCmd_GetTiles",
    "AnimCmd_GetPalette",
    "AnimCmd_JumpBack",
    "AnimCmd_End",
    "AnimCmd_PlaySoundEffect",
    "AnimCmd_6",
    "AnimCmd_TranslateSprite",
    "AnimCmd_8",
    "AnimCmd_SetIdAndVariant",
    "AnimCmd_10",
    "AnimCmd_11",
    "AnimCmd_12",

    // NOTE(Jace): This is NOT a "real" command, but a
    // notification for the game that it should
    // display a specfic frame, and for how long.
    // Thanks to @MainMemory_ for reminding me on how they work!
    "AnimCmd_Display",
};

const char* macroNames[SizeofArray(animCommands)] = {
    "mGetTiles",
    "mGetPalette",
    "mJumpBack",
    "mEnd",
    "mPlaySoundEffect",
    "mAnimCmd6",
    "mTranslateSprite",
    "mAnimCmd8",
    "mSetIdAndVariant",
    "mAnimCmd10",
    "mAnimCmd11",
    "mAnimCmd12",

    "mDisplayFrame"
};

// %s placeholders:
// 1) Macro name      (e.g. 'mGetTiles')
// 2) Cmd identifier  (e.g. 'AnimCmd_GetTiles')
const char* macros[SizeofArray(animCommands)] = {
    [~(AnimCmd_GetTiles)] =
        ".macro %s tile_index:req, num_tiles_to_copy:req\n"
        ".4byte %s\n"
        "  .4byte \\tile_index\n"
        "  .4byte \\num_tiles_to_copy\n"
        ".endm\n",

    [~(AnimCmd_GetPalette)] =
        ".macro %s pal_id:req, num_colors_to_copy:req, insert_offset:req\n"
        ".4byte %s\n"
        "  .4byte \\pal_id\n"
        "  .2byte \\num_colors_to_copy\n"
        "  .2byte \\insert_offset\n"
        ".endm\n",

    [~(AnimCmd_JumpBack)] =
        ".macro %s offset:req\n"
        ".4byte %s\n"
        "  .4byte \\offset\n"
        ".endm\n",

    [~(AnimCmd_4)] =
        ".macro %s\n"
        ".4byte %s\n"
        ".endm\n",

    [~(AnimCmd_PlaySoundEffect)] =
        ".macro %s songId:req\n"
        ".4byte %s\n"
        "  .2byte \\songId\n"
        "  .space 2\n" /* Padding */
        ".endm\n",

    // TODO: Parameters might be wrong
    [~(AnimCmd_6)] =
        ".macro %s unk4:req, unk8:req, unk9:req, unkA:req, unkB:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        "  .byte \\unk8, \\unk9, \\unkA, \\unkB\n"
        ".endm\n",

    [~(AnimCmd_TranslateSprite)] =
        ".macro %s x:req y:req\n"
        ".4byte %s\n"
        "  .2byte \\x\n"
        "  .2byte \\y\n"
        ".endm\n",

    // TODO: Parameters might be wrong
    [~(AnimCmd_8)] =
        ".macro %s unk4:req, unk8:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        "  .4byte \\unk8\n"
        ".endm\n",

    [~(AnimCmd_SetIdAndVariant)] =
        ".macro %s animId:req, variant:req\n"
        ".4byte %s\n"
        "  .2byte \\animId\n"
        "   .2byte \\variant\n"
        ".endm\n",

    [~(AnimCmd_10)] =
        ".macro %s unk4:req, unk8:req, unkC:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        "  .4byte \\unk8\n"
        "  .4byte \\unkC\n"
        ".endm\n",

    [~(AnimCmd_11)] =
        ".macro %s unk4:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        ".endm\n",

    [~(AnimCmd_12)] =
        ".macro %s unk4:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        ".endm\n",


    [~(AnimCmd_DisplayFrame)] =
        ".macro %s displayFor:req frameIndex:req\n"
        "  .4byte \\displayFor, \\frameIndex\n"
        ".endm\n",
};

static void printAnimationTable(FILE* fileStream, DynTable* dynTable, AnimationTable* animTable, LabelStrings* labels);
static u16 countVariants(u8* rom, AnimationTable* animTable, u32 animId);
static StringId pushLabel(LabelStrings* db, MemArena* stringArena, MemArena* offsetArena, char* label);


static long int
getFileSize(FILE* file) {
    // Get file stream's position
    long int prevPos = ftell(file);

    long int size;

    // Get file size
    fseek(file, 0, SEEK_END);
    size = ftell(file);

    // Set file stream offset to its previous offset
    fseek(file, prevPos, SEEK_SET);

    return size;
}

static void*
romToVirtual(u8* rom, u32 gbaPointer) {
    // GBA ROM Pointers can only go from 0x08000000 to 0x09FFFFFF (32MB max.)
    u8 pointerMSB = ((gbaPointer & 0xFF000000) >> 24);

    if (pointerMSB == 8 || pointerMSB == 9)
        return (u32*)(rom + (gbaPointer & 0x00FFFFFF));
    else
        return NULL;
}

// TODO: Can indenting be done with character codes?
static void
printHeaderLine(FILE* fileStream, const char* name, int value, int rightAlign) {
    // Print the amount of table entries
    fprintf(fileStream, ".equ %s,", name);

    // Print the indent
    s16 indentSpaces = rightAlign - strlen(name);
    for (; indentSpaces > 0; indentSpaces--)
        fprintf(fileStream, " ");
    
    // Print the value
    fprintf(fileStream, "%d\n", value);
}

static void
printMacros(FILE* fileStream) {
    for (int i = 0; i < SizeofArray(macros); i++) {
        fprintf(fileStream, macros[i], macroNames[i], animCommands[i]);
        fprintf(fileStream, "\n");
    }
}

static void
printFileHeader(FILE* fileStream, s32 entryCount) {
    // Set the section
    fprintf(fileStream, "\t.section .rodata\n");
    fprintf(fileStream, "\n");

    const char* entryCountName = "NUM_ANIMATION_TABLE_ENTRIES";

    // Find the biggest string out of the 'animCommands' array
    s16 rightAlign = strlen(entryCountName);
    for (int i = 0; i < SizeofArray(animCommands); i++)
        rightAlign = Max(rightAlign, strlen(animCommands[i]));

    // Space behind the comma
    rightAlign += 1;

    // Print definition of each Cmd's constant
    for(int i = 0; i < SizeofArray(animCommands); i++) {
        printHeaderLine(fileStream, animCommands[i], ((-1) - i), rightAlign);
    }
    fprintf(fileStream, "\n");

    // Print the number of entries in the table
    printHeaderLine(fileStream, entryCountName, entryCount, rightAlign);
    fprintf(fileStream, "\n\n");

    // Macros depend on knowing the AnimCmd_xyz values, so they have to be printed together.
    printMacros(fileStream);
}

static char*
getStringFromId(LabelStrings* label, StringId id) {
    char* result = NULL;

    if (id < label->count) {
        result = &label->strings[label->offsets[id]];
    }

    return result;
}

static void
printCommand(FILE* fileStream, DynTableAnimCmd* inAnimCmd, LabelStrings* labels) {
    ACmd* inCmd = &inAnimCmd->cmd;

    // Print macro name
    s32 nottedCmdId = ~(inCmd->cmdId);
    if (nottedCmdId >= 0)
        fprintf(fileStream, "\t%s ", macroNames[nottedCmdId]);
    else
        fprintf(fileStream, "\t%s ", macroNames[~(AnimCmd_DisplayFrame)]);


    // Print the command paramters
    switch (inCmd->cmdId) {
    case AnimCmd_GetTiles: {
        ACmd_GetTiles* cmd = &inCmd->_tiles;
        fprintf(fileStream, "0x%X %d\n", cmd->tileIndex, cmd->numTilesToCopy);
    } break;

    case AnimCmd_GetPalette: {
        ACmd_GetPalette* cmd = &inCmd->_pal;
        fprintf(fileStream, "0x%X %d 0x%X\n", cmd->palId, cmd->numColors, cmd->insertOffset);
    } break;

    case AnimCmd_JumpBack: {
        ExCmd_JumpBack* cmd = &inCmd->_exJump;
        StringId jmpCmdLabel = inAnimCmd->label;
        StringId targetLabel = ((DynTableAnimCmd*)cmd->jumpTarget)->label;
        char* jmpCmdString = getStringFromId(labels, jmpCmdLabel);
        char* targetCmdString = getStringFromId(labels, targetLabel);
        fprintf(fileStream, "((%s-%s) / %zd)\n\n", jmpCmdString, targetCmdString, sizeof(s32));
    } break;

    case AnimCmd_4: {
        ACmd_4* cmd = &inCmd->_end;
        fprintf(fileStream, "\n\n");
    } break;

    case AnimCmd_PlaySoundEffect: {
        ACmd_PlaySoundEffect* cmd = &inCmd->_sfx;
        fprintf(fileStream, "%u\n", cmd->songId);

    } break;

    case AnimCmd_6: {
        ACmd_6* cmd = &inCmd->_6;
        fprintf(fileStream, "0x%X 0x%X 0x%X 0x%X 0x%X\n",
            cmd->unk4.unk0, cmd->unk4.unk4, cmd->unk4.unk5, cmd->unk4.unk6, cmd->unk4.unk7);
    } break;

    case AnimCmd_TranslateSprite: {
        ACmd_TranslateSprite* cmd = &inCmd->_translate;
        fprintf(fileStream, "%d %d\n", cmd->x, cmd->y);
    } break;

    case AnimCmd_8: {
        ACmd_8* cmd = &inCmd->_8;
        fprintf(fileStream, "0x%x 0x%x", cmd->unk4, cmd->unk8);
    } break;

    case AnimCmd_SetIdAndVariant: {
        ACmd_SetIdAndVariant* cmd = &inCmd->_animId;

        // TODO: Insert ANIM_<whatever> from "include/constants/animations.h"
        fprintf(fileStream, "%d %d\n", cmd->animId, cmd->variant);
    } break;

    case AnimCmd_10: {
        ACmd_10* cmd = &inCmd->_10;
        fprintf(fileStream, "0x%x 0x%x 0x%x", cmd->unk4, cmd->unk8, cmd->unkC);
    } break;

    case AnimCmd_11: {
        ACmd_11* cmd = &inCmd->_11;
        fprintf(fileStream, "0x%x", cmd->unk4);
    } break;

    case AnimCmd_12: {
        ACmd_12* cmd = &inCmd->_12;
        fprintf(fileStream, "0x%x", cmd->unk4);
    } break;

    default: {
        ACmd_Display* cmd = &inCmd->_display;
        if (cmd->cmdId < 0) {
            // This shouldn't be reached.
            fprintf(stderr,
                "Exporting failed, impossible state reached.\n"
                "Animation Command was invalid: %d", cmd->cmdId);
            exit(-1);
        }
        else {
            if (cmd->cmdId >= ROM_BASE || cmd->frameIndex >= ROM_BASE) {
                // @BUG! If we land here, that means a pointer was mistaken as an "unknown command"
                fprintf(fileStream, "\t.4byte 0x%07X, 0x%07X\n\n", cmd->displayForNFrames, cmd->frameIndex);
                assert(FALSE);
            } else
                fprintf(fileStream, "0x%X 0x%X\n\n", cmd->displayForNFrames, cmd->frameIndex);
        }
    }
    }
}

static void
printAnimationDataFile(FILE* fileStream, DynTable* dynTable,
    LabelStrings* labels, MemArena *stringArena, MemArena* stringOffsetArena,
    u32 numAnims, OutFiles* outFiles) {
    AnimationData anim;
    
    printFileHeader(outFiles->header, numAnims);

    char filename[256];

    DynTableAnim* table = dynTable->animations;
    u16* variantCounts = dynTable->variantCounts;

    for (int i = 0; i < numAnims; i++) {
        if (table[i].offsetVariants >= 0) {
            s32* variantOffsets = (s32*)OffsetPointer(&table[i].offsetVariants);
            u16 numVariants = variantCounts[i];

            char labelBuffer[256];
            char* animName = getStringFromId(labels, table[i].name);

            // Print all variants' commands
            for (int variantId = 0; variantId < numVariants; variantId++) {
                // currCmd -> start of variant
                s32* offset = &variantOffsets[variantId];
                DynTableAnimCmd* currCmd = (DynTableAnimCmd*)OffsetPointer(offset);

                currCmd->flags |= ACMD_FLAG__IS_START_OF_ANIM;

                int labelId = 0;
                while (TRUE) {
                    // Maybe print label

                    if (currCmd->flags & (ACMD_FLAG__IS_START_OF_ANIM | ACMD_FLAG__NEEDS_LABEL)) {
                        sprintf(labelBuffer, "%s__variant_%d_l%d", animName, variantId, labelId);

                        StringId varLabel = pushLabel(labels, stringArena, stringOffsetArena, labelBuffer);
                        currCmd->label = varLabel;
                        fprintf(fileStream, "%s: @ %07X\n", labelBuffer, currCmd->address);
                        labelId++;
                        
                    }

                    printCommand(fileStream, currCmd, labels);

                    // Break loop after printing jump/end command
                    if((currCmd->cmd.cmdId == AnimCmd_4)
                    || (currCmd->cmd.cmdId == AnimCmd_JumpBack)
                    || (currCmd->cmd.cmdId == AnimCmd_SetIdAndVariant))
                        break;

                    currCmd++;
                }
            }

            if (table[i].name) { // Print variant pointers
                char* entryName = getStringFromId(labels, table[i].name);
                if (entryName)
                    fprintf(fileStream, "%s:\n", entryName);

                for (int variantId = 0; variantId < numVariants; variantId++) {
                    fprintf(fileStream, "\t.4byte %s__variant_%d_l%d\n", animName, variantId, 0);
                }
                fprintf(fileStream, "\n\n");
            }
        }
    }
}

static bool
wasReferencedBefore(AnimationTable* animTable, int entryIndex, int* prevIndex) {
    s32* cursor = animTable->data;

    bool wasReferencedBefore = FALSE;

    for (int index = 0; index < entryIndex; index++) {
        if (cursor[index] == cursor[entryIndex]) {
            wasReferencedBefore = TRUE;

            if (prevIndex)
                *prevIndex = index;

            break;
        }
    }

    return wasReferencedBefore;
}

static void
printAnimationTable(FILE* fileStream, DynTable* dynTable, AnimationTable* table, LabelStrings* labels) {
    const char* animTableVarName = "gAnimations";

    fprintf(fileStream,
            "\n"
            ".align 2, 0\n"
            ".global %s\n"
            "%s:\n", animTableVarName, animTableVarName);
    
    s32 numAnims = table->entryCount;
    for(int i = 0; i < numAnims; i++) {
        if(table->data[i]) {
            int prevReferenceIndex;
            int animId = -1;
            
            if (wasReferencedBefore(table, i, &prevReferenceIndex))
                animId = prevReferenceIndex;
            else
                animId = i;

            assert(animId >= 0 && animId < numAnims);

            char* animName = getStringFromId(labels, dynTable->animations[animId].name);
            fprintf(fileStream, "\t.4byte %s\n", animName);
        } else {
            fprintf(fileStream, "\t.4byte 0\n");
        }
    }
    fprintf(fileStream, ".size %s,.-%s\n\n", animTableVarName, animTableVarName);
}

#define getRomRegion(rom) (rom[0xAF])

// TODO: Check a few more things in the ROM Header
static int
getRomIndex(u8* rom) {
    eGame result = UNKNOWN;

    if (!strncmp((rom + 0xA0), "SONIC ADVANCASOP", 16ul)) {
        result = SA1;
    }

    if(!strncmp((rom + 0xA0), "SONICADVANC2A2N", 15ul) // NTSC
    || !strncmp((rom + 0xA0), "SONIC ADVANCA2N", 15ul) // PAL
        ) {
        result = SA2;
    }

    if (!strncmp((rom + 0xA0), "SONIC ADVANCB3SP8P", 18ul)) {
        result = SA3;
    }
    
    if (!strncmp((rom + 0xA0), "AGB KIRBY AMB8K", 15ul)) {
        result = KATAM;
    }

    return result;
}

void getSpriteTables(u8* rom, int gameIndex, SpriteTables* tables) {
    RomPointer* result = NULL;

    char region = getRomRegion(rom);

    switch (gameIndex) {
    case 1: {
        //SA1 (PAL)
        result = romToVirtual(rom, 0x0801A78C); // SA1(PAL)
        result = romToVirtual(rom, *result);
    } break;

    case 2: {
        result = romToVirtual(rom, 0x0801A5DC); // SA2(PAL, NTSC)
        result = romToVirtual(rom, *result);
    } break;

    case 3: {
        result = romToVirtual(rom, 0x08000404);// SA3(PAL, NTSC)
        result = romToVirtual(rom, *result);

    } break;

    // Kirby ATAM
    case 10: {
        result = romToVirtual(rom, 0x080002E0); // (PAL, maybe others?)
        result = romToVirtual(rom, *result);
    }

    default: {
        memset(tables, 0, sizeof(*tables));
    }
    }

    SpriteTablesROM* romTable = (SpriteTablesROM*)result;

    tables->animations = romToVirtual(rom, romTable->animations);
    tables->dimensions = romToVirtual(rom, romTable->dimensions);
    tables->oamData    = romToVirtual(rom, romTable->oamData);
    tables->palettes   = romToVirtual(rom, romTable->palettes);
    tables->tiles_4bpp = romToVirtual(rom, romTable->tiles_4bpp);
    tables->tiles_8bpp = romToVirtual(rom, romTable->tiles_8bpp);
}

// Input:
// NOTE: Indices can go from 0-255 -> count can be 256, so count has to be a u16
static u16
countVariants(u8* rom, AnimationTable* animTable, u32 animId) {
    u16 count = 0;

    RomPointer anim = animTable->data[animId];
    if (anim != 0) {
        RomPointer* variants = romToVirtual(rom, anim);

        for (;;) {
            // Check whether we're at a pointer,
            // and not the beginning of 'animTable' (which points at the individual anims)
            if((romToVirtual(rom, variants[count]) != NULL)
/* SA1 corner-case */ && ((RomPointer*)romToVirtual(rom, variants[count]) < variants)
            && (&variants[count] < animTable->data)) {
                // Found another variant
                count++;
            }
            else {
                // No more variants found
                break;
            }
        }
    }

    return count;
}

DynTableAnimCmd*
fillVariantFromRom(MemArena* arena, u8* rom, const RomPointer* variantInRom) {
    ACmd* cmdInRom = romToVirtual(rom, *variantInRom);   // A 'real' pointer to the current cmd inside the ROM
    RomPointer cmdAddress = *variantInRom;               // The ROM pointer the current cmd is at

    DynTableAnimCmd* variantStart = memArenaReserve(arena, sizeof(DynTableAnimCmd));
    DynTableAnimCmd* currCmd = variantStart;

    bool breakLoop = FALSE;
    while (!breakLoop && (void*)cmdInRom < (void*)variantInRom) {
        currCmd->address = cmdAddress;

        u32 structSize = 0;

        if (cmdInRom->cmdId < 0) {
            currCmd->cmd.cmdId = cmdInRom->cmdId;

            switch (cmdInRom->cmdId) {
            case AnimCmd_GetTiles:
                currCmd->cmd._tiles.tileIndex      = cmdInRom->_tiles.tileIndex;
                currCmd->cmd._tiles.numTilesToCopy = cmdInRom->_tiles.numTilesToCopy;
                structSize = sizeof(ACmd_GetTiles);
                break;

            case AnimCmd_GetPalette:
                currCmd->cmd._pal.palId        = cmdInRom->_pal.palId;
                currCmd->cmd._pal.numColors    = cmdInRom->_pal.numColors;
                currCmd->cmd._pal.insertOffset = cmdInRom->_pal.insertOffset;
                structSize = sizeof(ACmd_GetPalette);
                break;

                // This sets the jump-address, to make it easier to
                // find the commands needing a headline.
            case AnimCmd_JumpBack:
                currCmd->cmd._jump.offset = cmdInRom->_jump.offset;
                currCmd->jmpTarget = cmdAddress - cmdInRom->_jump.offset*sizeof(s32);
                structSize = sizeof(ACmd_JumpBack);

                // We will use this label to calculate the offset for the jump
                currCmd->flags |= ACMD_FLAG__NEEDS_LABEL;

                for (DynTableAnimCmd* check = variantStart; check < currCmd; check++) {
                    if (check->address == currCmd->jmpTarget) {
                        check->flags |= ACMD_FLAG__IS_POINTED_TO;

                        StringId labelId = check->label;

                        currCmd->cmd._exJump.jumpTarget = check;
                        break;
                    }
                }

                breakLoop = TRUE;
                break;

                // 'End' command
            case AnimCmd_4:
                structSize = sizeof(ACmd_4);
                breakLoop = TRUE;
                break;

            case AnimCmd_PlaySoundEffect:
                currCmd->cmd._sfx.songId = cmdInRom->_sfx.songId;
                structSize = sizeof(ACmd_PlaySoundEffect);
                break;


            case AnimCmd_6:
                currCmd->cmd._6.unk4.unk0 = cmdInRom->_6.unk4.unk0;
                currCmd->cmd._6.unk4.unk4 = cmdInRom->_6.unk4.unk4;
                currCmd->cmd._6.unk4.unk5 = cmdInRom->_6.unk4.unk5;
                currCmd->cmd._6.unk4.unk6 = cmdInRom->_6.unk4.unk6;
                currCmd->cmd._6.unk4.unk7 = cmdInRom->_6.unk4.unk7;
                structSize = sizeof(ACmd_6);
                break;

            case AnimCmd_TranslateSprite:
                currCmd->cmd._translate.x = cmdInRom->_translate.x;
                currCmd->cmd._translate.y = cmdInRom->_translate.y;
                structSize = sizeof(ACmd_TranslateSprite);
                break;

            case AnimCmd_8:
                currCmd->cmd._8.unk4 = cmdInRom->_8.unk4;
                currCmd->cmd._8.unk8 = cmdInRom->_8.unk8;
                structSize = sizeof(ACmd_8);
                break;

            case AnimCmd_SetIdAndVariant:
                currCmd->cmd._animId.animId  = cmdInRom->_animId.animId;
                currCmd->cmd._animId.variant = cmdInRom->_animId.variant;
                structSize = sizeof(ACmd_SetIdAndVariant);

                breakLoop = TRUE;
                break;

            case AnimCmd_10:
                currCmd->cmd._10.unk4 = cmdInRom->_10.unk4;
                currCmd->cmd._10.unk8 = cmdInRom->_10.unk8;
                currCmd->cmd._10.unkC = cmdInRom->_10.unkC;
                structSize = sizeof(ACmd_10);
                break;
                
            case AnimCmd_11:
                currCmd->cmd._11.unk4 = cmdInRom->_11.unk4;
                structSize = sizeof(ACmd_11);
                break;
                
            case AnimCmd_12:
                currCmd->cmd._12.unk4 = cmdInRom->_12.unk4;
                structSize = sizeof(ACmd_12);
                break;
            }
        }
        else {
            // NOTE: If the "word" here is negative, it is actually a COMMAND!
            //       A corner-case thanks to anim_0777 in SA1...
            if (cmdInRom->cmdId > ROM_BASE) {
                ;
            }
            else {
                currCmd->cmd._display.displayForNFrames = cmdInRom->_display.displayForNFrames;
                currCmd->cmd._display.frameIndex = cmdInRom->_display.frameIndex;

                structSize = sizeof(ACmd_Display);
            }
        }

        cmdInRom = (ACmd*)(((u8*)cmdInRom) + structSize);
        cmdAddress += structSize;

        // Prevent an empty cmd getting allocated,
        // when the loop is about to end
        if(!breakLoop)
            currCmd = memArenaReserve(arena, sizeof(DynTableAnimCmd));
    }

    return variantStart;
}

/* +--------------------------------------+
   |  ---------  Data Layout  ---------   |
   +--------------------------------------+
   |  DynTableAnim[]                      |
   +--------------------------------------+
   |  u16[animCount] variantsPerAnim      |
   +--------------------------------------+
   | (s32)offsets -> each anim's variants |
   |  / / / / / / / / / / / / / / / / / / |
   | All commands, for each variant       |
   +--------------------------------------+
*/
static void
createDynamicAnimTable(MemArena* arena, u8* rom, AnimationTable *animTable, DynTable* dynTable) {
    u32 animCount = animTable->entryCount;

    DynTableAnim* table = NULL;
    u16* variantsPerAnim = NULL;
    DynTableAnimCmd* variantStart = NULL;

    // Init the table and ensure there's enough space in memory
    table = memArenaReserve(arena, animCount * sizeof(DynTableAnim));

    // Count the number of variants of each animation
    variantsPerAnim = memArenaReserve(arena, animCount * sizeof(u16));
    for (u32 animId = 0; animId < animCount; animId++)
        variantsPerAnim[animId] = countVariants(rom, animTable, animId);


    // Convert all the commands of each animation into a format that lets us easily modify it.
    for (int animationId = 0; animationId < animCount; animationId++) {
        RomPointer animPointer = animTable->data[animationId];
        if (animPointer == 0)
            continue;

        // Don't print an animation that we already printed.
        int prevIndex = -1;
        if (wasReferencedBefore(animTable, animationId, &prevIndex)) {
            s32 offsetCurrPrev = (&table[prevIndex] - &table[animationId]);

            table[animationId].offsetVariants = offsetCurrPrev;
            variantsPerAnim[animationId] = variantsPerAnim[prevIndex];

            continue;
        }

        RomPointer *variantsInRom = romToVirtual(rom, animPointer);

        // allocate offsets of each variant
        s32* variantOffsets = memArenaReserve(arena, variantsPerAnim[animationId] * sizeof(u32));
        table[animationId].offsetVariants = (s32)((u8*)variantOffsets - (u8*)&table[animationId]);


        // - iterate through all variants of the current animation
        // - flag commands that are pointed at by jumps.
        // - set offset to each variant
        u16 numVariants = variantsPerAnim[animationId];
        for (u16 variantId = 0; variantId < numVariants; variantId++) {
            variantStart = fillVariantFromRom(arena, rom, &variantsInRom[variantId]);

            s32 offset = (s32)(((u8*)variantStart) - (u8*)&variantOffsets[variantId]);
            variantOffsets[variantId] = offset;
        }

        // SA1 cornercase, of a pointer pointing at
        // supposedly deleted variant, without a replacement "End" command.
        if((variantsInRom[numVariants] >= ROM_BASE)
        && (&variantsInRom[numVariants] < animTable->data)){

        }
    }
    
    dynTable->animations = table;
    dynTable->variantCounts = variantsPerAnim;
}

static StringId
pushLabel(LabelStrings* db, MemArena* stringArena, MemArena* offsetArena, char* label) {
    char* string = memArenaAddString(stringArena, label);
    s32* offset  = memArenaReserve(offsetArena, sizeof(s32));
    *offset = (s32)(string - (char*)stringArena->memory);
    db->count++;

    return db->count - 1;
}

static void
createAnimLabels(DynTable* table, u32 numAnimations, LabelStrings* labels, MemArena* stringArena, MemArena* stringOffsetArena) {
    labels->strings = stringArena->memory;
    labels->offsets = stringOffsetArena->memory;
    labels->count   = 0;

    // Push empty string as "Dummy" value.
    pushLabel(labels, stringArena, stringOffsetArena, "");

    // Load animation-name labels
    // TODO: Implement loading main animation-labels from file.
    char buffer[256];
    for (u32 animId = 0; animId < numAnimations; animId++) {
        // We need to check 'name' as well, since we assigned
        // the offsets for already-referenced anims in 'createDynamicAnimTable'
        DynTableAnim* current = &table->animations[animId];

        if(current->offsetVariants != 0) {
            StringId stringId = 0;

            // (offset < 0) -> references previous entry, 
            if (current->offsetVariants < 0) {
                DynTableAnim* previous = (DynTableAnim*)OffsetPointer(&current->offsetVariants);

                stringId = previous->name;
            }
            else {
                sprintf(buffer, "anim_%04d", animId);
                stringId = pushLabel(labels, stringArena, stringOffsetArena, buffer);
            }
            current->name = stringId;
        }
    }

    labels->strings = stringArena->memory;
    labels->offsets = stringOffsetArena->memory;
}

typedef void (*CmdIterator)(FILE* fileStream, DynTableAnimCmd* cmd, u16 animId, u16 variantId, u16 labelId, void* itParams);

// Go through every command that was found in the game and pass it to 'iterator' function.
void iterateAllCommands(FILE* fileStream, DynTable *dynTable, u16 startAnimId, u16 endAnimId, CmdIterator iterator, void* iteratorParams) {
    for (int animId = startAnimId; animId < endAnimId; animId++) {
        DynTableAnim* anim = &dynTable->animations[animId];

        // Already gone over this anim
        if (anim->offsetVariants < 0)
            continue;

        s32* variOffsets = (s32*)OffsetPointer(&anim->offsetVariants);
        u16 variantCount = dynTable->variantCounts[animId];
        for (int variantId = 0; variantId < variantCount; variantId++) {
            DynTableAnimCmd* dtCmd = (DynTableAnimCmd*)OffsetPointer(&variOffsets[variantId]);

            int labelId = 0;
            while (TRUE) {
                iterator(fileStream, dtCmd, animId, variantId, labelId, iteratorParams);

                dtCmd++;

                if((dtCmd->cmd.cmdId == AnimCmd_4)
                || (dtCmd->cmd.cmdId == AnimCmd_JumpBack)
                //|| (dtCmd->cmd.cmdId == AnimCmd_SetIdAndVariant)
                || (dtCmd->cmd.cmdId < AnimCmd_12))
                    break;
            }
        }
    }
}

typedef struct {
    s32 firstTileId;
    u32 minRange;
    s32 paletteId;
} TileRange;

typedef struct {
    MemArena* tileRanges;

    // Tiles
    u32 numGetTileCalls;
    u32 numTileIndices;

    // Palette
    s32 cachedPaletteId;
    s16 cachedNumColors;
    s16 cachedInsertOffset;

    s16 cachedWidth;
    s16 cachedHeight;
} TileInfo;

void itGetNumTileInformation(FILE* fileStream, DynTableAnimCmd* dtCmd, u16 animId, u16 variantId, u16 labelId, void* itParams) {
    TileInfo* tileInfo = (TileInfo*)itParams;

    if (dtCmd->cmd.cmdId == AnimCmd_GetPalette) {
        ACmd_GetPalette* cmd = &dtCmd->cmd._pal;

        tileInfo->cachedPaletteId = cmd->palId;
        tileInfo->cachedNumColors = cmd->numColors;
        tileInfo->cachedInsertOffset = cmd->insertOffset;
    }
    if (dtCmd->cmd.cmdId == AnimCmd_GetTiles) {
        ACmd_GetTiles* cmd = &dtCmd->cmd._tiles;

        if (cmd->tileIndex < 0) {
#if 0
            fprintf(fileStream,
                "--- 8BPP FOUND: %d\n"
                "  Anim: %d, Vari: %d, Label: %d\n"
                "   Pal:  %d, Num: %d, Insrt: %d\n",
                cmd->tileIndex,
                animId, variantId, labelId,
                tileInfo->cachedPaletteId, tileInfo->cachedNumColors, tileInfo->cachedInsertOffset);
#endif
        }
        else {
#if 0
            fprintf(fileStream,
                "Pal:  %6d, Num: %4d, Insrt: %2d\n",
                tileInfo->cachedPaletteId, tileInfo->cachedNumColors, tileInfo->cachedInsertOffset);
#endif
        }

        TileInfo* tileInfo = (TileInfo*)itParams;
        tileInfo->numTileIndices = Max(tileInfo->numTileIndices, (cmd->tileIndex + cmd->numTilesToCopy));
        tileInfo->numGetTileCalls++;

        TileRange tr;
        tr.firstTileId = cmd->tileIndex;
        tr.minRange    = cmd->numTilesToCopy;
        tr.paletteId   = tileInfo->cachedPaletteId;
        memArenaAddMemory(tileInfo->tileRanges, &tr, sizeof(tr));

    }
}

int trCompare(const void* _a, const void* _b) {
    TileRange* a = (TileRange*)_a;
    TileRange* b = (TileRange*)_b;

    long long aDiff = (long long)a->firstTileId | (long long)a->minRange;
    long long bDiff = (long long)b->firstTileId | (long long)b->minRange;
    return(a - b);
}

void generateScriptBinaryToPng(TileRange* tr, char* scriptPath, int firstFrame, int lastFrame) {
    FILE* script_BinaryToPng = fopen(scriptPath, "a+");

    // @NOTE(Jace): When using this script, make sure the files aren't restricted!
    fprintf(script_BinaryToPng, "#!/bin/sh\n");
    for (int frame = firstFrame; frame < lastFrame; frame++) {
        int palId = 0;

        // TODO: Replace this with the actual information provided by the SpriteTable
        // Afterwards remove math.h from this and -lm from build.sh.
        int frameWidth = 3;
        int frameTileCount = tr[frame].minRange;
        int widthSqrt = sqrtf((float)frameTileCount);
        if (frameTileCount == (widthSqrt * widthSqrt))
            frameWidth = widthSqrt;
        else while ((frameWidth < frameTileCount) && (frameTileCount % frameWidth) != 0)
            frameWidth++;

        frameWidth = Min(frameTileCount, frameWidth);
        fprintf(script_BinaryToPng,
            "./gbagfx ../frames/frame_%04d.4bpp ../frames/frame_%04d.png -object -palette ../palettes/pal_%03d.gbapal -width %d\n",
            frame, frame, tr[frame].paletteId, frameWidth);
    }
}

// Behaviour similar to 'updateDirectory' but doesn't try to
// create a directory from the file path.
char* addToPath(MemArena* arena, char* path, char* name) {
    char* out = memArenaReserve(arena, strlen(path) + strlen("/") + strlen(name) + 1);
    sprintf(out, "%s/%s", path, name);
    return out;
}

char* updateDirectory(MemArena* arena, char* currentPath, char* newFolder) {
    char* out = NULL;

    if (newFolder == NULL) {
        out = memArenaAddString(arena, currentPath);
    } else {
        out = addToPath(arena, currentPath, newFolder);
    }
    mkdir(out);

    return out;
}

char* gameFolderName(u8* rom) {
    eGame index = getRomIndex(rom);

    switch (index) {
    case SA1: {
        return "sa1";
    }
    case SA2: {
        return "sa2";
    }
    case SA3: {
        return "sa3";
    }
    case KATAM: {
        return "katam";
    }
    default:
        return "";
    }
}

void tryLoadingRom(char* path, u8** rom, eGame *romIndex) {
    FILE* romFile = fopen(path, "rb");
    int fileSize = 0;

    if (romFile == NULL) {
        fprintf(stderr, "Could not open file '%s'. Code: %d\n", path, errno);
        exit(-2);
    }

    fileSize = getFileSize(romFile);
    *rom = (u8*)malloc(fileSize);

    fseek(romFile, 0, SEEK_SET);
    if (fread(*rom, 1, fileSize, romFile) != fileSize) {
        fprintf(stderr, "File '%s' couldn't be fully loaded.\n", path);
        exit(-3);
    }

    // We have a copy of the ROM in memory
    // and don't intend to write to it again, so close the file.
    fclose(romFile);


    *romIndex = getRomIndex(*rom);
    if (*romIndex == UNKNOWN) {
        fprintf(stderr, "Loaded ROM is unknown game. Closing...\n");
        exit(-4);
    }
}

void printHelp(char* programPath) {
    fprintf(stderr,
        "This program can be used to extract animation data from the Sonic Advance games.\n"
        "Please add the path to a Sonic Advance 1|2|3 ROM file as a parameter.\n"
        "%s <SA3 ROM>\n", programPath);
}

// Get the last occurence of sub in base
char* lastString(char* base, char* sub) {
    if (!base || !sub)
        return NULL;

    int baseLen = strlen(base);
    int subLen  = strlen(sub);

    if (baseLen < subLen)
        return NULL;

    char* last   = base + baseLen - subLen;
    char* cursor = last;
    while (cursor >= base) {
        if (cursor = strstr(cursor, sub))
            break;
        else
            cursor = --last;
    }

    // String was not found
    if (cursor < base)
        return NULL;
    else
        return cursor;
}

int main(int argCount, char** args) {
    if((argCount < 2) || (argCount > 2)
    || (!strcmp(args[1], "-h"))
    || (!strcmp(args[1], "--help"))) {
        printHelp(args[0]);
        exit(-1);
    }
    
    u8* rom = NULL;

    eGame game;
    tryLoadingRom(args[1], &rom, &game);


    SpriteTables spriteTables;
    getSpriteTables(rom, game, &spriteTables);

    AnimationTable animTable = { 0 };
    animTable.data = spriteTables.animations;
    animTable.entryCount = g_TotalAnimationCount[game];

    OutFiles files = { 0 };
#if !PRINT_TO_STDOUT
    // Create output directories
    MemArena paths;
    memArenaInit(&paths);

    // Directory paths
    char* outPath       = updateDirectory(&paths, "out", NULL);
    char* gameAssetPath = updateDirectory(&paths, outPath, gameFolderName(rom));
    char* palettePath   = updateDirectory(&paths, gameAssetPath, "palettes");
    char* framePath     = updateDirectory(&paths, gameAssetPath, "frames");
    char* docsPath      = updateDirectory(&paths, gameAssetPath, "documents");

    // File paths
    char* headerFilePath          = addToPath(&paths, docsPath, "macros.inc");
    char* animationTableFilePath  = addToPath(&paths, docsPath, "animation_table.inc");
    char* genFramesScriptFilePath = addToPath(&paths, docsPath, "gen_frames.sh");
    
    files.header    = fopen(headerFilePath, "w");
    files.animTable = fopen(animationTableFilePath, "w");
#endif
    if (!files.header)
        files.header = stdout;

    if (!files.animTable)
        files.animTable = stdout;

    MemArena mtableArena;
    memArenaInit(&mtableArena);
                    
    MemArena stringOffsetArena;
    memArenaInit(&stringOffsetArena);
                    
    MemArena stringArena;
    memArenaInit(&stringArena);

    DynTable dynTable = { 0 };
    createDynamicAnimTable(&mtableArena, rom, &animTable, &dynTable);

    // Generates the names for the animations themselves
    LabelStrings labels = { 0 };
    createAnimLabels(&dynTable, animTable.entryCount, &labels, &stringArena, &stringOffsetArena);

#if 1
    printAnimationDataFile(files.header, &dynTable, &labels, &stringArena, &stringOffsetArena, animTable.entryCount, &files);
    printAnimationTable(files.animTable, &dynTable, &animTable, &labels);
#endif
    MemArena tileRanges;
    memArenaInit(&tileRanges);

    TileInfo tileInfo = { 0 };
    tileInfo.tileRanges = &tileRanges;

    iterateAllCommands(stdout, &dynTable, 0, animTable.entryCount, itGetNumTileInformation, &tileInfo);
    //printf("Calls: %d\nTile Indices: %d\n", tileInfo.numGetTileCalls, tileInfo.numTileIndices);


    // Sort all calls by the tile index
    TileRange* tr = (TileRange*)tileRanges.memory;
    qsort(tr, tileInfo.numGetTileCalls, sizeof(TileRange), trCompare);

    // Count and only keep the unique calls
    int uniqueCallCount = 1;
    for (int i = 1; i < tileInfo.numGetTileCalls; i++) {
        TileRange* a = &tr[i - 1];
        TileRange* b = &tr[i];

        if (a->firstTileId - b->firstTileId) {
            tr[uniqueCallCount++] = *b;
        }
                        
    }
    //printf("COUNT: %d\n", uniqueCallCount);

    /* Now we have all tile-ranges in the game, in order. */

    char *filePath = addToPath(&paths, framePath, "frame_XXXX.4bpp");
    char* fileString = lastString(filePath, "frame_");
    u16 paletteBuffer[16*16] = { 0 };
#define OUTPUT_FRAMES 0
#if OUTPUT_FRAMES
    // Output every frame as its own file.
    for (int i = 0; i < uniqueCallCount; i++) {
        TileRange* a = &tr[i];

        u8* spriteFrame = spriteTables.tiles_4bpp + a->firstTileId * TILE_SIZE_4BPP;


        sprintf(fileString, "frame_%04d.4bpp", i);
        FILE* frameFile = fopen(filePath, "wb");
        fwrite(spriteFrame, TILE_SIZE_4BPP, a->minRange, frameFile);
        fclose(frameFile);
    }
#endif

#define OUTPUT_PALETTES 1
#if OUTPUT_PALETTES
    int colorsPerPalette = 16;
    int paletteCount = ((spriteTables.tiles_4bpp - (u8*)spriteTables.palettes) / (2*colorsPerPalette));

    char* fileName = addToPath(&paths, palettePath, "pal_XXXXXX.gbapal");
    fileName = lastString(fileName, "pal_");
    // Output every palette as its own file.
    for (int i = 0; i < paletteCount; i++) {

        u16* pal = spriteTables.palettes + colorsPerPalette * i;

        memcpy(&paletteBuffer, pal, 2 * 16);

        sprintf(fileName, "pal_%03d.gbapal", i);
        FILE* palFile = fopen(palettePath, "wb");
        fwrite(paletteBuffer, 2, 16, palFile);
        fclose(palFile);
    }
#endif

    int firstFrame = 0;
    int lastFrame = uniqueCallCount;// Min(firstFrame + 5, uniqueCallCount);
    generateScriptBinaryToPng(tr, genFramesScriptFilePath, firstFrame, lastFrame);


    if(files.animTable && files.animTable != stdout)
        fclose(files.animTable);

    if (files.header && files.header != stdout)
        fclose(files.header);

    return 0;
}
