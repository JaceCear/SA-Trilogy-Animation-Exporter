#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>


#include "types.h"
#include "arenaAlloc.h"

// Needs to be included before animExporter.h
#include "animation_commands.h"

#include "animExporter.h"

#define SizeofArray(array) ((sizeof(array)) / (sizeof(array[0])))

#define PointerFromOffset(base, offset) (((u8*)(base)) + (offset))


// TODO(Jace): Add custom animation table size
#define SA1_ANIMATION_COUNT    908
#define SA2_ANIMATION_COUNT    1133
#define SA3_ANIMATION_COUNT    1524

const u32 g_TotalAnimationCount[3] = {
    SA1_ANIMATION_COUNT,
    SA2_ANIMATION_COUNT,
    SA3_ANIMATION_COUNT,
};

// Creating separate files for each animation takes a lot of time (4 seconds for me),
// so print to stdout per default, which is instant if it's directed to a file with ' > file.out'
#define PRINT_TO_STDOUT TRUE

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

    // This is NOT a "real" command, but a
    // notification for the game than it should
    // display the frame, and for how long.
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
        ".macro %s unk4:req, unk8:req\n"
        ".4byte %s\n"
        "  .4byte \\unk4\n"
        "  .4byte \\unk8\n"
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
        fprintf(fileStream, "0x%X 0x%X\n", cmd->unk4, cmd->unk8);
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
                fprintf(fileStream, "%d %d\n\n", cmd->displayForNFrames, cmd->frameIndex);
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
            s32* variantOffsets = (s32*)PointerFromOffset(&table[i], table[i].offsetVariants);
            u16 numVariants = variantCounts[i];

            char labelBuffer[256];
            char* animName = getStringFromId(labels, table[i].name);

            // Print all variants' commands
            for (int variantId = 0; variantId < numVariants; variantId++) {
                // currCmd -> start of variant
                s32* offset = &variantOffsets[variantId];
                DynTableAnimCmd* currCmd = (DynTableAnimCmd*)PointerFromOffset(offset, *offset);

                currCmd->flags |= ACMD_FLAG__IS_START_OF_ANIM;

                int labelId = 0;
                while (TRUE) {
                    // Maybe print label

                    if (currCmd->flags & (ACMD_FLAG__IS_START_OF_ANIM | currCmd->flags & ACMD_FLAG__NEEDS_LABEL)) {
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
    int result = 0;

    if (!strncmp((rom + 0xA0), "SONIC ADVANCASOP", 16ul)) {
        // Strings are equal, and it's SA1, so return 1
        result = 1;
    }

    if(!strncmp((rom + 0xA0), "SONICADVANC2A2N", 15ul) // NTSC
    || !strncmp((rom + 0xA0), "SONIC ADVANCA2N", 15ul) // PAL
        ) {
        // Strings are equal, and it's SA2, so return 2
        result = 2;
    }

    if (!strncmp((rom + 0xA0), "SONIC ADVANCB3SP8P", 18ul)) {
        // Strings are equal, and it's SA3, so return 3
        result = 3;
    }

    return result;
}

u32* getAnimTableAddress(u8* rom, int gameIndex) {
    u32* result = NULL;

    char region = getRomRegion(rom);

    switch (gameIndex) {
    case 1: {
        //SA1 (PAL)
        result = romToVirtual(rom, 0x0801A78C); // SA1(PAL)
        result = romToVirtual(rom, *result);
        result = romToVirtual(rom, *result);
    } break;

    case 2: {
        result = romToVirtual(rom, 0x080113E8); // SA2(PAL, NTSC)
        result = romToVirtual(rom, *result);
    } break;

    case 3: {
        result = romToVirtual(rom, 0x08000404);// SA3(PAL, NTSC)
        result = romToVirtual(rom, *result);
        result = romToVirtual(rom, *result);

    } break;

    default: {
        ;
    }
    }

    return result;
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

    int romIndex = getRomIndex(rom);

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
                currCmd->cmd._6.unk4 = cmdInRom->_6.unk4;
                currCmd->cmd._6.unk8 = cmdInRom->_6.unk8;
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
                DynTableAnim* previous = (DynTableAnim*)PointerFromOffset(current, current->offsetVariants);

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

int main(int argCount, char** args) {
    int result = 0;

    if(argCount != 2 || !strcmp(args[1], "-h") || !strcmp(args[1], "--help")) {
        printf("This program can be used to extract animation data from the Sonic Advance games.\n"
               "Currently only a single file gets put out through stdout, but in the future this might change.\n\n");
        
        printf("Please add the path to a Sonic Advance 1|2|3 ROM file as a parameter.\n");
        printf("%s <SA3 ROM>\n", args[0]);
        result = -1;
    } else {
        FILE* romFile = fopen(args[1], "rb");
        u8 *rom = 0;
        int fileSize = 0;
        AnimationTable animTable = {0};
        
        if(romFile) {
            fileSize = getFileSize(romFile);
            rom = (u8*)malloc(fileSize);
            
            fseek(romFile, 0, SEEK_SET);
            if (fread(rom, 1, fileSize, romFile) == fileSize) {
                fclose(romFile);

                int romIndex = getRomIndex(rom);
                if (romIndex) {
                    // 0x08000404: Points to animation data struct in SA3
                    u32* address = getAnimTableAddress(rom, romIndex);

                    // 0x080113E8 -> anim table
                    // SA2 anim table should be: 0x08135EC4

                    animTable.data = address;
                    animTable.entryCount = g_TotalAnimationCount[romIndex - 1];

                    OutFiles files = { 0 };
#if !PRINT_TO_STDOUT
                    files.header    = fopen("out/macros.inc", "w");
                    files.animTable = fopen("out/animation_table.inc", "w");
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

                    printAnimationDataFile(stdout, &dynTable, &labels, &stringArena, &stringOffsetArena, animTable.entryCount, &files);


                    printAnimationTable(files.animTable, &dynTable, &animTable, &labels);

                    if(files.animTable != NULL && files.animTable != stdout)
                        fclose(files.animTable);

                    if (files.header != NULL && files.header != stdout)
                        fclose(files.header);
                }
            }
            else {
                fprintf(stderr, "File '%s' couldn't be fully loaded.\n", args[1]);
                result = -2;
            }
        } else {
            fprintf(stderr, "Could not open file '%s'. Code: %d\n", args[1], errno);
            result = -3;
        }
    }
    return result;
}
