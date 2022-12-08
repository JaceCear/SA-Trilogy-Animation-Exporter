#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "types.h"
#include "arenaAlloc.h"

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


static void printAnimationTable(u8 *rom, AnimationTable *table, FILE *fileStream);

char* animCommands[] = {
    "AnimCmd_GetTileIndex",
    "AnimCmd_GetPalette",
    "AnimCmd_JumpBack",
    "AnimCmd_4",
    "AnimCmd_PlaySoundEffect",
    "AnimCmd_6",
    "AnimCmd_7",
    "AnimCmd_8",
    "AnimCmd_9",
    "AnimCmd_10",
    "AnimCmd_11",
    "AnimCmd_12",
};

// Some puzzle pieces are missing for this... :(
s8 cmdWordSize[] = {
    /* AnimCmd_GetTileIndex */    3,
    /* AnimCmd_GetPalette */      3,
    /* AnimCmd_JumpBack */       -1, // Negative entry implies a "jump" command
    /* AnimCmd_4 */               0,
    /* AnimCmd_PlaySoundEffect */ 2,
    /* AnimCmd_6 */               3,
    /* AnimCmd_7 */               2,
    /* AnimCmd_8 */               3,
    /* AnimCmd_9 */               2,
    /* AnimCmd_10 */              4,
    /* AnimCmd_11 */              2,
    /* AnimCmd_12 */              2,
};

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
romToVirtual(u8 *rom, u32 gbaPointer) {
    // GBA ROM Pointers can only go from 0x08000000 to 0x09FFFFFF (32MB max.)
    u8 pointerMSB = ((gbaPointer & 0xFF000000) >> 24);
    
    if(pointerMSB == 8 || pointerMSB == 9)
        return (u32*)(rom + (gbaPointer & 0x00FFFFFF));
    else
        return NULL;
}

static void
printFileHeader(FILE *fileStream, AnimationTable* table) {
    fprintf(fileStream, "\t.section .rodata\n");
    
    fprintf(fileStream, "\n");
    
    // Print referencese to each table entry
    for(int i = 0; i < SizeofArray(animCommands); i++) {
        fprintf(fileStream, ".equ %s,\t\t\t%d\n", animCommands[i], (-1) - i);
    }
    fprintf(fileStream, "\n");

    // Print the amount of table entries
    fprintf(fileStream, ".equ %s,\t\t\t%d\n", "NUM_ANIMATION_TABLE_ENTRIES", table->entryCount);

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

static void
printAnimationDataFile(u8* rom, AnimationTable *animTable, FILE* fileStream) {
    AnimationData anim;
    
    s32 *cursor = animTable->data;
    
    printFileHeader(fileStream, animTable);
    
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
                bool cursorIsAtStartOfSubAnim = (anim.cursor == romToVirtual(rom, anim.base[nextSubIndex]));

                if((nextSubIndex < anim.subCount) && cursorIsAtStartOfSubAnim) {
                    
                    // Label for sub animation
                    fprintf(fileStream,
                            /*"\n.global anim_data__%04d_%d" // No need to make them global, right? */
                            "\n\tanim_data__%04d_%d:", i, nextSubIndex);
                    
                    nextSubIndex++;
                }
                
                if(*anim.cursor < 0 && *anim.cursor >= -12) {
                    // Print command
                    fprintf(fileStream,
                            "\n"
                            "\t\t.4byte\t%s", animCommands[~(*anim.cursor)]);
                } else {
                    // NOTE(Jace): There's a corner-case in SA1, where
                    //             anim_777 starts with 0xC, not a command.
                    //             This gets around that.
                    if (cursorIsAtStartOfSubAnim) {
                        // Print said corner-case value
                        fprintf(fileStream,
                            "\n"
                            "\t\t.4byte\t0x%X", *anim.cursor);
                    }
                    else {
                        // print command parameter
                        fprintf(fileStream, ", 0x%X", *anim.cursor);
                    }
                }
                
                anim.cursor++;
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
        FILE* fileStream = fopen(args[1], "rb+");
        u8 *rom = 0;
        int fileSize = 0;
        AnimationTable animTable = {0};
        
        if(fileStream) {
            fileSize = getFileSize(fileStream);
            rom = (u8*)malloc(fileSize);
            
            fseek(fileStream, 0, SEEK_SET);
            if (fread(rom, 1, fileSize, fileStream) == fileSize) {
                fclose(fileStream);

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
