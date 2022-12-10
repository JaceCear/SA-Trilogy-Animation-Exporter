#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "arenaAlloc.h"

#include "animation_commands.h"

#define SizeofArray(array) ((sizeof(array)) / (sizeof(array[0])))


// TODO(Jace): Add custom animation table size
#define SA1_ANIMATION_COUNT    908
#define SA2_ANIMATION_COUNT    1133
#define SA3_ANIMATION_COUNT    1524

const u32 g_TotalSpriteStateCount[3] = {
    SA1_ANIMATION_COUNT,
    SA2_ANIMATION_COUNT,
    SA3_ANIMATION_COUNT,
};


typedef struct {
    u32 *data;
    s32 entryCount;
} AnimationTable;

typedef struct {
    u32 *base; // Always(?) points backwards
    s32 *cursor;
    s32 subCount; // There can be multiple "sub animations" in one entry.
} AnimationData;

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

const char* animCommands[] = {
    "AnimCmd_GetTiles",
    "AnimCmd_GetPalette",
    "AnimCmd_JumpBack",
    "AnimCmd_4",
    "AnimCmd_PlaySoundEffect",
    "AnimCmd_6",
    "AnimCmd_TranslateSprite",
    "AnimCmd_8",
    "AnimCmd_SetIdAndVariant",
    "AnimCmd_10",
    "AnimCmd_11",
    "AnimCmd_12",
};

const char* macroNames[] = {
    "mGetTiles",
    "mGetPalette",
    "mJumpBack",
    "mAnimCmd4",
    "mPlaySoundEffect",
    "mAnimCmd6",
    "mTranslateSprite",
    "mAnimCmd8",
    "mSetIdAndVariant",
    "mAnimCmd10",
    "mAnimCmd11",
    "mAnimCmd12",
};

// %s placeholders:
// 1) Macro name      (e.g. 'mGetTiles')
// 2) Cmd identifier  (e.g. 'AnimCmd_GetTiles')
const char* macros[] = {
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
};

static void printAnimationTable(u8* rom, AnimationTable* table, FILE* fileStream);

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
printFileHeader(FILE* fileStream, AnimationTable* table) {
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
    printHeaderLine(fileStream, entryCountName, table->entryCount, rightAlign);
    fprintf(fileStream, "\n\n");
}

bool
wasReferencedBefore(AnimationTable *animTable, int entryIndex, int *prevIndex) {
    s32 *cursor = animTable->data;
    
    bool wasReferencedBefore = FALSE;
    
    for(int index = 0; index < entryIndex; index++) {
        if(cursor[index] == cursor[entryIndex]) {
            wasReferencedBefore = TRUE;
            
            if(prevIndex)
                *prevIndex = index;
            
            break;
        }
    }
    
    return wasReferencedBefore;
}

static void* printCommand(FILE* fileStream, void* inCursor) {
    s32* cursor = inCursor;

    if (*cursor >= 0)
        return cursor;

    // Print macro name
    s32 cmdId = ~(*cursor);
    fprintf(fileStream, "\t\t%s ", macroNames[cmdId]);


    // Print the commands
    switch (*cursor) {
    case AnimCmd_GetTiles: {
        ACmd_GetTiles* cmd = inCursor;

        fprintf(fileStream, "0x%X %d\n", cmd->tileIndex, cmd->numTilesToCopy);

        cursor += AnimCommandSizeInWords(ACmd_GetTiles);
    } break;

    case AnimCmd_GetPalette: {
        ACmd_GetPalette* cmd = inCursor;

        fprintf(fileStream, "0x%X %d 0x%X\n", cmd->palId, cmd->numColors, cmd->insertOffset);

        cursor += AnimCommandSizeInWords(ACmd_GetPalette);
    } break;

    case AnimCmd_JumpBack: {
        ACmd_JumpBack* cmd = inCursor;

        fprintf(fileStream, "0x%X\n", cmd->offset);

        cursor += AnimCommandSizeInWords(ACmd_JumpBack);
    } break;

    case AnimCmd_4: {
        fprintf(fileStream, "\n");
        cursor += AnimCommandSizeInWords(ACmd_4);
    } break;

    case AnimCmd_PlaySoundEffect: {
        ACmd_PlaySoundEffect* cmd = inCursor;

        fprintf(fileStream, "%u\n", cmd->songId);

        cursor += AnimCommandSizeInWords(ACmd_PlaySoundEffect);

    } break;

    case AnimCmd_6: {
        ACmd_6* cmd = inCursor;

        fprintf(fileStream, "0x%X 0x%X\n", cmd->unk4, cmd->unk8);

        cursor += AnimCommandSizeInWords(ACmd_6);
    } break;

    case AnimCmd_TranslateSprite: {
        ACmd_TranslateSprite* cmd = inCursor;

        fprintf(fileStream, "%d %d\n", cmd->x, cmd->y);

        cursor += AnimCommandSizeInWords(ACmd_TranslateSprite);
    } break;

    case AnimCmd_8: {
        ACmd_8* cmd = inCursor;

        fprintf(fileStream, "0x%x 0x%x", cmd->unk4, cmd->unk8);

        cursor += AnimCommandSizeInWords(ACmd_8);
    } break;

    case AnimCmd_SetIdAndVariant: {
        ACmd_SetIdAndVariant* cmd = inCursor;

        // TODO: Insert ANIM_<whatever> from "include/constants/animations.h"
        fprintf(fileStream, "%d %d\n", cmd->animId, cmd->variant);

        cursor += AnimCommandSizeInWords(ACmd_SetIdAndVariant);
    } break;

    case AnimCmd_10: {
        ACmd_10* cmd = inCursor;

        fprintf(fileStream, "0x%x 0x%x 0x%x", cmd->unk4, cmd->unk8, cmd->unkC);

        cursor += AnimCommandSizeInWords(ACmd_10);
    } break;

    case AnimCmd_11: {
        ACmd_11* cmd = inCursor;

        fprintf(fileStream, "0x%x", cmd->unk4);

        cursor += AnimCommandSizeInWords(ACmd_11);
    } break;

    case AnimCmd_12: {
        ACmd_12* cmd = inCursor;

        fprintf(fileStream, "0x%x", cmd->unk4);

        cursor += AnimCommandSizeInWords(ACmd_12);
    } break;

    default: {
        // This shouldn't be reached.
        fprintf(stderr, "Expoting failed, impossible state reached.\n");
        exit(-1);
    }
    }

    return cursor;
}


static void
printAnimationDataFile(u8* rom, AnimationTable *animTable, FILE* fileStream) {
    AnimationData anim;
    
    s32 *cursor = animTable->data;
    
    printFileHeader(fileStream, animTable);
    printMacros(fileStream);

    for(int i = 0; i < animTable->entryCount; i++) {
        if(cursor[i]) {
            
            if(wasReferencedBefore(animTable, i, 0))
                continue;
            
            anim.base   = romToVirtual(rom, cursor[i]);
            anim.cursor = romToVirtual(rom, *anim.base);
            
            {// Get the number of sub-animations and store them in 'anim'
                int subAnimCount = 0;
                
                for(;;) {
                    s32 subAnimRomPtr = anim.base[subAnimCount];
                    
                    if(romToVirtual(rom, subAnimRomPtr) &&
                       (&anim.base[subAnimCount] != animTable->data)) {
                        subAnimCount++;
                    } else {
                        break;
                    }
                }
                
                anim.subCount = subAnimCount;
            }
            
            fprintf(fileStream, "\n.align 2, 0");
            int nextSubIndex = 0;
            while((void*)anim.cursor < (void*)anim.base) {
                bool cursorIsAtStartOfVariant = (anim.cursor == romToVirtual(rom, anim.base[nextSubIndex]));

                if((nextSubIndex < anim.subCount) && cursorIsAtStartOfVariant) {
                    
                    // Label for sub animation
                    fprintf(fileStream,
                            /*"\n.global anim_data__%04d_%d" // No need to make them global, right? */
                            "\n\tanim_data__%04d_%d:\n", i, nextSubIndex);
                    
                    nextSubIndex++;
                }
                
                bool isCursorAtCmd = (*anim.cursor < 0 && *anim.cursor >= -12);
                if (isCursorAtCmd) {
                    anim.cursor = printCommand(fileStream, anim.cursor);
                } else {
                    // NOTE(Jace): If instead of a command, the cursor finds a positive number
                    //             it is some yet not documented pair of numbers, which get printed together.
                    //             It does not matter, whether the 2nd number is positive or negative.

                    fprintf(fileStream, "\t\t.4byte\t%d, %d", anim.cursor[0], anim.cursor[1]);
                    anim.cursor += 2;

                    fprintf(fileStream, "\n\n");
                }
                
            }
            
            fprintf(fileStream,
                    "\n"
                    "\n"
                    ".align 2, 0\n"
                    "\tanim_%04d:\n", i);
            
            // Print (sub) animation pointer(s)
            for(int subIndex = 0; subIndex < anim.subCount; subIndex++) {
                fprintf(fileStream, "\t\t.4byte anim_data__%04d_%d\n", i, subIndex);
            }
            
            fprintf(fileStream, "\n\n\n");
            
        }
    }
    
    printAnimationTable(rom, animTable, fileStream);
}

static void
printAnimationTable(u8 *rom, AnimationTable *table, FILE *fileStream) {
    const char* animTableVarName = "gSpriteAnimations";

    fprintf(fileStream,
            ".align 2, 0\n"
            ".global %s\n"
            "%s:\n", animTableVarName, animTableVarName);
    
    for(int i = 0; i < table->entryCount; i++) {
        if(table->data[i]) {
            int prevReferenceIndex;
            int animId = -1;
            
            if (wasReferencedBefore(table, i, &prevReferenceIndex))
                animId = prevReferenceIndex;
            else
                animId = i;

            assert(animId >= 0 && animId < table->entryCount);

            fprintf(fileStream, "\t.4byte anim_%04d\n", animId);
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
                    animTable.entryCount = g_TotalSpriteStateCount[romIndex - 1];

                    printAnimationDataFile(rom, &animTable, stdout);
                }
            }
            else {
                fprintf(stderr, "File '%s' couldn't be fully loaded.\n", args[1]);
                result = -2;
            }
        } else {
            fprintf(stderr, "Could not open file '%s'. Code: %d\n", args[1], errno);
            result = -2;
        }
    }
    return result;
}
