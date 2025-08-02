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


#include "types.h"
#include "ArenaAlloc.h"

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
#define AnimCmd_End             -4
#define AnimCmd_PlaySoundEffect -5
#define AnimCmd_AddHitbox       -6
#define AnimCmd_TranslateSprite -7
#define AnimCmd_8               -8
#define AnimCmd_SetIdAndVariant -9
#define AnimCmd_10              -10
#define AnimCmd_SetSpritePriority              -11
#define AnimCmd_12              -12
#define AnimCmd_DisplayFrame    (AnimCmd_12-1)

const char* animCommands[] = {
    "AnimCmd_GetTiles",
    "AnimCmd_GetPalette",
    "AnimCmd_JumpBack",
    "AnimCmd_End",
    "AnimCmd_PlaySoundEffect",
    "AnimCmd_AddHitbox",
    "AnimCmd_TranslateSprite",
    "AnimCmd_8",
    "AnimCmd_SetIdAndVariant",
    "AnimCmd_10",
    "AnimCmd_SetSpritePriority",
    "AnimCmd_12",
    
    // NOTE(Jace): This is NOT a "real" command, but a
    // notification for the game that it should
    // display a specific frame, and for how long.
    // Thanks to @MainMemory_ for reminding me on how they work!
    "AnimCmd_Display",
};

const char* animCommandsC[] = {
    "ANIM_CMD__TILES",
    "ANIM_CMD__PALETTE",
    "ANIM_CMD__JUMP_BACK",
    "ANIM_CMD__END",
    "ANIM_CMD__PLAY_SOUND",
    "ANIM_CMD__HITBOX",
    "ANIM_CMD__TRANSLATE",
    "ANIM_CMD__8",
    "ANIM_CMD__CHANGE_ANIM",
    "ANIM_CMD__10",
    "ANIM_CMD__SET_PRIORITY",
    "ANIM_CMD__12",
    
    "ANIM_CMD__SHOW_FRAME"
};

const char* macroNames[SizeofArray(animCommands)] = {
    "mGetTiles",
    "mGetPalette",
    "mJumpBack",
    "mEnd",
    "mPlaySoundEffect",
    "mAddHitbox",
    "mTranslateSprite",
    "mAnimCmd8",
    "mSetIdAndVariant",
    "mAnimCmd10",
    "mAnimCmdSetSpritePriority",
    "mAnimCmd12",
    
    "mDisplayFrame"
};

const char* macroNamesC[SizeofArray(animCommands)] = {
    "TILES",
    "PALETTE",
    "JUMP_BACK",
    "END",
    "PLAY_SOUND",
    "HITBOX",
    "TRANSLATE",
    "CMD_8",
    "CHANGE_ANIM",
    "CMD_10",
    "SET_PRIORITY",
    "CMD_12",
    
    "SHOW_FRAME"
};
// %s placeholders:
// 1) Macro name      (e.g. 'mGetTiles')
// 2) Cmd identifier  (e.g. 'AnimCmd_GetTiles')
// 3) Array-Base      (e.g. 'gObjPalettes')
const char* macrosAsm[SizeofArray(animCommands)] = {
    [~(AnimCmd_GetTiles)] =
        ".macro %s tile_index:req, num_tiles_to_copy:req\n"
        ".4byte %s\n"
        "  .4byte \\tile_index\n"
        "  .4byte \\num_tiles_to_copy\n"
        ".endm\n",
    
    [~(AnimCmd_GetPalette)] =
        ".macro %s pal_ptr:req, num_colors_to_copy:req, insert_offset:req\n"
        ".4byte %s\n"
        "  .4byte (\\pal_ptr - %s) / 0x20\n"
        "  .2byte \\num_colors_to_copy\n"
        "  .2byte \\insert_offset\n"
        ".endm\n",
    
    [~(AnimCmd_JumpBack)] =
        ".macro %s jmpTarget:req\n"
        ".4byte %s\n"
        "  .4byte ((.-0x4) - \\jmpTarget)\n"
        ".endm\n",
    
    [~(AnimCmd_End)] =
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
    [~(AnimCmd_AddHitbox)] =
        ".macro %s index:req, left:req, top:req, right:req, bottom:req\n"
        ".4byte %s\n"
        "  .4byte \\index\n"
        "  .byte \\left, \\top, \\right, \\bottom\n"
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
    
    [~(AnimCmd_SetSpritePriority)] =
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

// TODO: Make PALETTE take a pointer?
// TODO: Find a way to make JUMP_BACK relative, like in assembly.
//       It should be: (( (<offset location> - sizeof(u32)) - target ) / sizeof(u32))
const char *macrosC[SizeofArray(animCommands)] = {
    [~(AnimCmd_GetTiles)]   =
        "#define TILES(index, count)                     %s, index, count,\n",
    [~(AnimCmd_GetPalette)] =
        "#define PALETTE(num, count, offset)             %s, num, num, (((u16)count << 0) | ((u16)offset << 16)),\n",
    [~(AnimCmd_JumpBack)] = 
        "#define JUMP_BACK(offset)                       %s, offset,\n",
    [~(AnimCmd_End)] = 
        "#define END()                                   %s,\n",
    [~(AnimCmd_PlaySoundEffect)] = 
        "#define PLAY_SOUND(id)                          %s, id,\n",
    [~(AnimCmd_AddHitbox)] = 
        "#define HITBOX(index, left, top, right, bottom) %s, index, (((left & 0xFF) << 0) | ((top & 0xFF) << 8) | ((right & 0xFF) << 16) | ((bottom & 0xFF) << 24)),\n",
    [~(AnimCmd_TranslateSprite)] = 
        "#define TRANSLATE(x, y)                         %s, (((u16)x << 0) | ((u16)y << 16)),\n",
    [~(AnimCmd_8)] = 
        "#define CMD_8(a, b)                             %s, a, b,\n",
    [~(AnimCmd_SetIdAndVariant)] = 
        "#define CHANGE_ANIM(anim, variant)              %s, (((u16)anim << 0) | ((u16)variant << 16)),\n",
    [~(AnimCmd_10)] = 
        "#define CMD_10(a, b, c)                         %s, a, b, c,\n",
    [~(AnimCmd_SetSpritePriority)] = 
        "#define SET_PRIORITY(prio)                      %s, prio,\n",
    [~(AnimCmd_12)] = 
        "#define CMD_12(a)                               %s, a,\n",
    [~(AnimCmd_DisplayFrame)] = 
        "#define SHOW_FRAME(duration, frameId)           duration, frameId,\n",
};

static void printAnimationTable(FILE* fileStream, DynTable* dynTable, AnimationTable* animTable, LabelStrings* labels, bool outputC);
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
printHeaderLine(FILE* fileStream, const char* name, int value, int rightAlign, bool outputC) {
    // Print the amount of table entries
    if(!outputC)
        fprintf(fileStream, ".equ %s,", name);
    else
        fprintf(fileStream, "#define %s", name);
    
    // Print the indent
    s16 indentSpaces = rightAlign - strlen(name);
    for (; indentSpaces > 0; indentSpaces--)
        fprintf(fileStream, " ");
    
    // Print the value
    fprintf(fileStream, "%d\n", value);
}

static void
printMacros(FILE* fileStream, bool outputC) {
    const char **macros = (outputC) ? macrosC : macrosAsm;
    const char **names = (outputC) ? animCommandsC : macroNames;
    const char *palettesLabel = "gObjPalettes";
    
    for (int i = 0; i < SizeofArray(macrosC); i++) {
        if(i == ~AnimCmd_GetPalette) {
            if(outputC) {
                fprintf(fileStream, macros[i], names[i], palettesLabel, palettesLabel);
            } else {
                fprintf(fileStream, macros[i], names[i], animCommands[i], palettesLabel);
            }
        } else {
            fprintf(fileStream, macros[i], names[i]);
        }
    }
    fprintf(fileStream, "\n");
}

static void
printFileHeader(FILE* fileStream, s32 entryCount, bool outputC) {
    const char* entryCountName = "NUM_ANIMATION_TABLE_ENTRIES";
    const char **animCmds = (outputC) ? animCommandsC : animCommands;
    if(!outputC) {
        // Set the section
        fprintf(fileStream, "\t.section .rodata\n");
        fprintf(fileStream, "\n");
    }
    
    
    // Find the biggest string out of the 'animCommands' array
    s16 rightAlign = strlen(entryCountName);
    for (int i = 0; i < SizeofArray(animCommands); i++)
        rightAlign = Max(rightAlign, strlen(animCommands[i]));
    
    // Space behind the comma
    rightAlign += 1;
    
    // Print definition of each Cmd's constant
    for(int i = 0; i < SizeofArray(animCommands); i++) {
        printHeaderLine(fileStream, animCmds[i], ((-1) - i), rightAlign, outputC);
    }
    fprintf(fileStream, "\n");
    
    // Print the number of entries in the table
    printHeaderLine(fileStream, entryCountName, entryCount, rightAlign, outputC);
    fprintf(fileStream, "\n\n");
    
    // Macros depend on knowing the AnimCmd_xyz values, so they have to be printed together.
    printMacros(fileStream, outputC);
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
    s32 nottedCmdId = ~(inCmd->id);
    if (nottedCmdId >= 0)
        fprintf(fileStream, "\t%s ", macroNames[nottedCmdId]);
    else
        fprintf(fileStream, "\t%s ", macroNames[~(AnimCmd_DisplayFrame)]);
    
    
    // Print the command paramters
    switch (inCmd->id) {
        case AnimCmd_GetTiles: {
            ACmd_GetTiles* cmd = &inCmd->_tiles;
            fprintf(fileStream, "0x%X %d\n", cmd->tileIndex, cmd->numTilesToCopy);
        } break;
        
        case AnimCmd_GetPalette: {
            ACmd_GetPalette* cmd = &inCmd->_pal;
            char palName[64];
#if 0
            sprintf(palName, "palObj%03d", cmd->palId);
            fprintf(fileStream, "%s %d 0x%X\n", palName, cmd->numColors, cmd->insertOffset);
#else
            fprintf(fileStream, "%d %d 0x%X\n", cmd->palId, cmd->numColors, cmd->insertOffset);
#endif
        } break;
        
        case AnimCmd_JumpBack: {
            ExCmd_JumpBack* cmd = &inCmd->_exJump;
            StringId jmpCmdLabel = inAnimCmd->label;
            StringId targetLabel = ((DynTableAnimCmd*)cmd->jumpTarget)->label;
            char* jmpCmdString = getStringFromId(labels, jmpCmdLabel);
            char* targetCmdString = getStringFromId(labels, targetLabel);
            fprintf(fileStream, "%s\n\n", targetCmdString);
        } break;
        
        case AnimCmd_End: {
            ACmd_End* cmd = &inCmd->_end;
            fprintf(fileStream, "\n\n");
        } break;
        
        case AnimCmd_PlaySoundEffect: {
            ACmd_PlaySoundEffect* cmd = &inCmd->_sfx;
            fprintf(fileStream, "%u\n", cmd->songId);
            
        } break;
        
        case AnimCmd_AddHitbox: {
            ACmd_AddHitbox* cmd = &inCmd->_hitbox;
            // TODO: Once the tool outputs data as C files, output these as signed bytes (ARM macros don't like '-xyz')
            fprintf(fileStream, "%d 0x%02X 0x%02X 0x%02X 0x%02X\n",
                    cmd->hitbox.index,
                    (u8)cmd->hitbox.left,
                    (u8)cmd->hitbox.top,
                    (u8)cmd->hitbox.right,
                    (u8)cmd->hitbox.bottom);
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
            fprintf(fileStream, "0x%x 0x%x 0x%x\n", cmd->unk4, cmd->unk8, cmd->unkC);
        } break;
        
        case AnimCmd_SetSpritePriority: {
            ACmd_SetSpritePriority* cmd = &inCmd->_prio;
            fprintf(fileStream, "0x%x\n", cmd->unk4);
        } break;
        
        case AnimCmd_12: {
            ACmd_12* cmd = &inCmd->_12;
            fprintf(fileStream, "0x%x\n", cmd->unk4);
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
printCommandC(FILE* fileStream, DynTableAnimCmd* inAnimCmd, LabelStrings* labels) {
    ACmd* inCmd = &inAnimCmd->cmd;
    
    // Print macro name
    s32 nottedCmdId = ~(inCmd->id);
    if (nottedCmdId >= 0)
        fprintf(fileStream, "    %s(", macroNamesC[nottedCmdId]);
    else
        fprintf(fileStream, "    %s(", macroNamesC[~(AnimCmd_DisplayFrame)]);
    
    
    // Print the command paramters
    switch (inCmd->id) {
        case AnimCmd_GetTiles: {
            ACmd_GetTiles* cmd = &inCmd->_tiles;
            fprintf(fileStream, "0x%X, %d)\n", cmd->tileIndex, cmd->numTilesToCopy);
        } break;
        
        case AnimCmd_GetPalette: {
            ACmd_GetPalette* cmd = &inCmd->_pal;
            char palName[64];
#if 0
            sprintf(palName, "palObj%03d", cmd->palId);
            fprintf(fileStream, "%s, %d, 0x%X)\n", palName, cmd->numColors, cmd->insertOffset);
#else
            fprintf(fileStream, "%d, %d, 0x%X)\n", cmd->palId, cmd->numColors, cmd->insertOffset);
#endif
        } break;
        
        case AnimCmd_JumpBack: {
            ExCmd_JumpBack* cmd = &inCmd->_exJump;
            StringId jmpCmdLabel = inAnimCmd->label;
            StringId targetLabel = ((DynTableAnimCmd*)cmd->jumpTarget)->label;
            char* jmpCmdString = getStringFromId(labels, jmpCmdLabel);
            char* targetCmdString = getStringFromId(labels, targetLabel);
            fprintf(fileStream, "%d)\n", cmd->offset);
        } break;
        
        case AnimCmd_End: {
            ACmd_End* cmd = &inCmd->_end;
            fprintf(fileStream, ")\n");
        } break;
        
        case AnimCmd_PlaySoundEffect: {
            ACmd_PlaySoundEffect* cmd = &inCmd->_sfx;
            fprintf(fileStream, "%u)\n", cmd->songId);
            
        } break;
        
        case AnimCmd_AddHitbox: {
            ACmd_AddHitbox* cmd = &inCmd->_hitbox;
            fprintf(fileStream, "%d, %d, %d, %d, %d)\n",
                    cmd->hitbox.index,
                    (s8)cmd->hitbox.left,
                    (s8)cmd->hitbox.top,
                    (s8)cmd->hitbox.right,
                    (s8)cmd->hitbox.bottom);
        } break;
        
        case AnimCmd_TranslateSprite: {
            ACmd_TranslateSprite* cmd = &inCmd->_translate;
            fprintf(fileStream, "%d, %d)\n", cmd->x, cmd->y);
        } break;
        
        case AnimCmd_8: {
            ACmd_8* cmd = &inCmd->_8;
            fprintf(fileStream, "0x%x, 0x%x)", cmd->unk4, cmd->unk8);
        } break;
        
        case AnimCmd_SetIdAndVariant: {
            ACmd_SetIdAndVariant* cmd = &inCmd->_animId;
            
            // TODO: Insert ANIM_<whatever> from "include/constants/animations.h"
            fprintf(fileStream, "%d, %d)\n", cmd->animId, cmd->variant);
        } break;
        
        case AnimCmd_10: {
            ACmd_10* cmd = &inCmd->_10;
            fprintf(fileStream, "0x%x, 0x%x, 0x%x)\n", cmd->unk4, cmd->unk8, cmd->unkC);
        } break;
        
        case AnimCmd_SetSpritePriority: {
            ACmd_SetSpritePriority* cmd = &inCmd->_prio;
            fprintf(fileStream, "0x%x)\n", cmd->unk4);
        } break;
        
        case AnimCmd_12: {
            ACmd_12* cmd = &inCmd->_12;
            fprintf(fileStream, "0x%x)\n", cmd->unk4);
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
                    fprintf(fileStream, "    .4byte 0x%07X, 0x%07X\n\n", cmd->displayForNFrames, cmd->frameIndex);
                    assert(FALSE);
                } else
                    fprintf(fileStream, "%d, %d)\n", cmd->displayForNFrames, cmd->frameIndex);
            }
        }
    }
}

static void
printAnimationDataFile(FILE* fileStream, DynTable* dynTable,
                       LabelStrings* labels, MemArena *stringArena, MemArena* stringOffsetArena,
                       u32 numAnims, OutFiles* outFiles, bool outputC) {
    AnimationData anim;
    
    printFileHeader(outFiles->header, numAnims, outputC);
    
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
                    
                    if(!outputC) {
                        if (currCmd->flags & (ACMD_FLAG__IS_START_OF_ANIM | ACMD_FLAG__NEEDS_LABEL)) {
                            sprintf(labelBuffer, "%s__v%d_l%d", animName, variantId, labelId);
                            
                            StringId varLabel = pushLabel(labels, stringArena, stringOffsetArena, labelBuffer);
                            currCmd->label = varLabel;
                            fprintf(fileStream, "%s: @ %07X\n", labelBuffer, currCmd->address);
                            labelId++;
                            
                        }
                    } else {
                        if(currCmd->flags & ACMD_FLAG__IS_START_OF_ANIM) {
                            sprintf(labelBuffer, "%s__v%d_l%d", animName, variantId, labelId);
                            
                            StringId varLabel = pushLabel(labels, stringArena, stringOffsetArena, labelBuffer);
                            currCmd->label = varLabel;
                            fprintf(fileStream, "const s32 %s[] = { // 0x%08X\n", labelBuffer, currCmd->address);
                            labelId++;
                        }
                    }
                    
                    if(!outputC)
                        printCommand(fileStream, currCmd, labels);
                    else
                        printCommandC(fileStream, currCmd, labels);
                    
                    // Add an additional newline after DisplayFrame cmd.
                    if(currCmd->cmd.id >= 0){
                        DynTableAnimCmd* nextCmd = currCmd + 1;
                        
                        if(!(nextCmd->flags & ACMD_FLAG__NEEDS_LABEL) || (nextCmd->cmd.id == AnimCmd_JumpBack))
                            fprintf(fileStream, "\n");
                        
                    }
                    
                    // Break loop after printing jump/end command
                    if((currCmd->cmd.id == AnimCmd_End)
                       || (currCmd->cmd.id == AnimCmd_JumpBack)
                       || (currCmd->cmd.id == AnimCmd_SetIdAndVariant)) {
                        if(outputC)
                            fprintf(fileStream, "};\n\n");
                        
                        break;
                    }
                    
                    currCmd++;
                }
                
                
            }
            
            if (table[i].name) { // Print variant pointers
                if(!outputC) {
                    char* entryName = getStringFromId(labels, table[i].name);
                    if (entryName)
                        fprintf(fileStream, "%s:\n", entryName);
                    
                    for (int variantId = 0; variantId < numVariants; variantId++) {
                        fprintf(fileStream, "\t.4byte %s__v%d_l%d\n", animName, variantId, 0);
                    }
                    fprintf(fileStream, "\n\n");
                } else {
                    char* entryName = getStringFromId(labels, table[i].name);
                    if (entryName)
                        fprintf(fileStream, "const s32 * const %s[%d] = {\n", entryName, numVariants);
                    
                    for (int variantId = 0; variantId < numVariants; variantId++) {
                        fprintf(fileStream, "    %s__v%d_l%d,\n", animName, variantId, 0);
                    }
                    fprintf(fileStream, "};\n\n");
                }
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
printAnimationTable(FILE* fileStream, DynTable* dynTable, AnimationTable* table, LabelStrings* labels, bool outputC) {
    const char* animTableVarName = "gAnimations";
    
    if(!outputC) {
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
    } else {
        s32 numAnims = table->entryCount;
        
        fprintf(fileStream, "#include \"global.h\"\n\n");
        
        // Resolve external references
        for(int i = 0; i < numAnims; i++) {
            if(table->data[i]) {
                fprintf(fileStream, "extern const s32 * const anim_%04d[];\n", i);
            }
        }
        fprintf(fileStream, "\n");
        
        fprintf(fileStream, "const s32 * const *%s[] = {\n", animTableVarName);
        
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
                fprintf(fileStream, "    %s,\n", animName);
            } else {
                fprintf(fileStream, "    NULL,\n");
            }
        }
        fprintf(fileStream, "};\n");
    }
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
        
        if (cmdInRom->id < 0) {
            currCmd->cmd.id = cmdInRom->id;
            
            switch (cmdInRom->id) {
            case AnimCmd_GetTiles: {
                currCmd->cmd._tiles.tileIndex      = cmdInRom->_tiles.tileIndex;
                currCmd->cmd._tiles.numTilesToCopy = cmdInRom->_tiles.numTilesToCopy;
                structSize = sizeof(ACmd_GetTiles);
            } break;
                
            case AnimCmd_GetPalette: {
                currCmd->cmd._pal.palId        = cmdInRom->_pal.palId;
                currCmd->cmd._pal.numColors    = cmdInRom->_pal.numColors;
                currCmd->cmd._pal.insertOffset = cmdInRom->_pal.insertOffset;
                structSize = sizeof(ACmd_GetPalette);
            } break;
                
                // This sets the jump-address, to make it easier to
                // find the commands needing a headline.
            case AnimCmd_JumpBack: {
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
            } break;
                
                // 'End' command
            case AnimCmd_End: {
                structSize = sizeof(ACmd_End);
                breakLoop = TRUE;
            } break;
                
            case AnimCmd_PlaySoundEffect: {
                currCmd->cmd._sfx.songId = cmdInRom->_sfx.songId;
                structSize = sizeof(ACmd_PlaySoundEffect);
            } break;
                
                
            case AnimCmd_AddHitbox: {
                currCmd->cmd._hitbox.hitbox.index = cmdInRom->_hitbox.hitbox.index;
                currCmd->cmd._hitbox.hitbox.left = cmdInRom->_hitbox.hitbox.left;
                currCmd->cmd._hitbox.hitbox.top = cmdInRom->_hitbox.hitbox.top;
                currCmd->cmd._hitbox.hitbox.right = cmdInRom->_hitbox.hitbox.right;
                currCmd->cmd._hitbox.hitbox.bottom = cmdInRom->_hitbox.hitbox.bottom;
                structSize = sizeof(ACmd_AddHitbox);
            } break;
                
            case AnimCmd_TranslateSprite: {
                currCmd->cmd._translate.x = cmdInRom->_translate.x;
                currCmd->cmd._translate.y = cmdInRom->_translate.y;
                structSize = sizeof(ACmd_TranslateSprite);
            } break;
                
            case AnimCmd_8: {
                currCmd->cmd._8.unk4 = cmdInRom->_8.unk4;
                currCmd->cmd._8.unk8 = cmdInRom->_8.unk8;
                structSize = sizeof(ACmd_8);
            } break;
                
            case AnimCmd_SetIdAndVariant: {
                currCmd->cmd._animId.animId  = cmdInRom->_animId.animId;
                currCmd->cmd._animId.variant = cmdInRom->_animId.variant;
                structSize = sizeof(ACmd_SetIdAndVariant);
                
                breakLoop = TRUE;
            } break;
                
            case AnimCmd_10: {
                currCmd->cmd._10.unk4 = cmdInRom->_10.unk4;
                currCmd->cmd._10.unk8 = cmdInRom->_10.unk8;
                currCmd->cmd._10.unkC = cmdInRom->_10.unkC;
                structSize = sizeof(ACmd_10);
            } break;
                
            case AnimCmd_SetSpritePriority: {
                currCmd->cmd._prio.unk4 = cmdInRom->_prio.unk4;
                structSize = sizeof(ACmd_SetSpritePriority);
            } break;
                
            case AnimCmd_12: {
                currCmd->cmd._12.unk4 = cmdInRom->_12.unk4;
                structSize = sizeof(ACmd_12);
            } break;
            }
        }
        else {
            // NOTE: If the "word" here is negative, it is actually a COMMAND!
            //       A corner-case thanks to anim_0777 in SA1...
            if (cmdInRom->id > ROM_BASE) {
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
                
                if((dtCmd->cmd.id == AnimCmd_End)
                   || (dtCmd->cmd.id == AnimCmd_JumpBack)
                   || (dtCmd->cmd.id == AnimCmd_SetIdAndVariant) // Enable for SA1 (only?)
                   || (dtCmd->cmd.id < AnimCmd_12))
                    break;
            }
        }
    }
}

typedef struct {
    s32 firstTileId;
    u32 minRange;
    s32 paletteId;
    
    u16 animId;
    u16 variantId;
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
} TileInfo;

void itGetNumTileInformation(FILE* fileStream, DynTableAnimCmd* dtCmd, u16 animId, u16 variantId, u16 labelId, void* itParams) {
    TileInfo* tileInfo = (TileInfo*)itParams;
    
    if (dtCmd->cmd.id >= 0) {
        ACmd_Display* disp = (ACmd_Display*)&dtCmd->cmd;
        TileInfo* tileInfo = (TileInfo*)itParams;
    }
    
    if (dtCmd->cmd.id == AnimCmd_GetPalette) {
        ACmd_GetPalette* cmd = &dtCmd->cmd._pal;
        
        tileInfo->cachedPaletteId = cmd->palId;
        tileInfo->cachedNumColors = cmd->numColors;
        tileInfo->cachedInsertOffset = cmd->insertOffset;
    }
    if (dtCmd->cmd.id == AnimCmd_GetTiles) {
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
        tr.animId      = animId;
        tr.variantId   = variantId;
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

// The "Display" command occurs after the tile/palette data is set,
// so we store the information in the buffer, until the command occurs.
static FrameData fdBuffer = { 0 };

void generateFrameData(FILE* fileStream, DynTableAnimCmd* dtCmd, u16 animId, u16 variantId, u16 labelId, void* itParams) {
    FrameDataInput* in = itParams;
    FrameData* frames  = in->data;
    
    if (dtCmd->cmd.id >= 0) {
        // Game says the frame shall be displayed, so we output it, if that didn't happen yet.
        ACmd_Display* cmd = &dtCmd->cmd._display;
        
        FrameData* frame = &frames[cmd->frameIndex];
        
        if (!frame->wasInitialized) {
            memcpy(frame, &fdBuffer, sizeof(fdBuffer));
            frame->animId = animId;
            frame->variantId = variantId;
            frame->labelId = labelId;
            frame->wasInitialized = TRUE;
            
            // Make sure the buffer doesn't immediate get copied in the next iteration
            fdBuffer.wasInitialized = FALSE;
        }
    }
    else if (dtCmd->cmd.id == AnimCmd_GetTiles) {
        ACmd_GetTiles* cmd = &dtCmd->cmd._tiles;
        fdBuffer.tileIndex = cmd->tileIndex;
        fdBuffer.tileCount = cmd->numTilesToCopy;
    }
    else if (dtCmd->cmd.id == AnimCmd_GetPalette) {
        ACmd_GetPalette* cmd = &dtCmd->cmd._pal;
        
        fdBuffer.paletteId = cmd->palId;
        fdBuffer.numColors = cmd->numColors;
    }
    else {
    }
}

// Return the number of frames in an animation
void getAnimFrameCount(FILE* fileStream, DynTableAnimCmd* dtCmd, u16 animId, u16 variantId, u16 labelId, void* itParams) {
    u16* result = (u16*)itParams;
    
    if (dtCmd->cmd.id >= 0) {
        ACmd_Display* disp = &dtCmd->cmd._display;
        
        // +1 because we want the _amount_ of frames
        *result = Max(disp->frameIndex+1, *result);
    }
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

typedef enum {
    GS_SQUARE,
    GS_HORIZONTAL,
    GS_VERTICAL,
} GBA_Shape;

typedef enum {
    GS_8x8,
    GS_16x16,
    GS_32x32,
    GS_64x64,
} GBA_Square;

typedef enum {
    GS_16x8,
    GS_32x8,
    GS_32x16,
    GS_64x32,
} GBA_Horz;

typedef enum {
    GS_8x16,
    GS_8x32,
    GS_16x32,
    GS_32x64,
} GBA_Vert;

typedef struct {
    s8 x, y;
} s8Vec2D;

typedef struct {
    s16 x, y;
} s16Vec2D;

const s8Vec2D sOamTileSizes[3][4] = {
    { {1,1}, {2,2}, {4,4}, {8,8} }, // Square
    { {2,1}, {4,1}, {4,2}, {8,4} }, // Horizontal
    { {1,2}, {1,4}, {2,4}, {4,8} }, // Vertical
};

typedef struct {
    u16 lastAnim;
    u16 writtenCount;
    MemArena arena;
} WrittenTiles;
static WrittenTiles writtenTiles;

// Check whether the addressed tiles were already exported.
bool wasFrameIndexed(u16 animId, u8* targetTiles) {
    if (animId != writtenTiles.lastAnim) {
        writtenTiles.writtenCount = 0;
        writtenTiles.lastAnim = animId;
    }
    
    u8** pointers = writtenTiles.arena.memory;
    
    bool result = FALSE;
    for (int i = 0; i < writtenTiles.writtenCount; i++) {
        if (pointers[i] == targetTiles) {
            result = TRUE;
            break;
        }
    }
    
    return result;
}

void indexFrame(void* frameTiles){
    u8** pointers = writtenTiles.arena.memory;
    
    pointers[writtenTiles.writtenCount] = frameTiles;
    writtenTiles.writtenCount++;
}

void generateSprite(u8* rom, MemArena* fullTileImage, SpriteTables* spriteTables, FrameDataInput* fdi, FILE* debugComposition, FILE* scriptFilestream, FILE* tile_collection, FILE* inc_bin, u16 animId, char* framePath, char* docsPath, char* palPath) {
    FrameData* fds = fdi->data;
    
    SpriteOffset* dimensions = romToVirtual(rom, spriteTables->dimensions[animId]);
    u16* oamDataStart        = romToVirtual(rom, spriteTables->oamData[animId]);
    
    // Write script header
    
    if (dimensions == NULL || oamDataStart == NULL)
        return;
    
    char filePath[256];
    char filenameNoExt[64];
    
    eGame game = getRomIndex(rom);
    
    for (int frameId = 0; frameId < fdi->frameCount; frameId++) {
        // Reset the frame buffer, to reduce memory usage
        fullTileImage->offset = 0;
        
        
        FrameData *fd = &fds[frameId];
        SpriteOffset* frameDimensions = &dimensions[frameId];
        assert((frameDimensions->width % 8) == 0);
        assert((frameDimensions->height % 8) == 0);
        
        // " X,  Y - SubCnt: [SubDim, SubPos] \n"
        fprintf(debugComposition, "%4d: %2d, %2d  - %4d  : ",
                animId,
                frameDimensions->width, frameDimensions->height,
                frameDimensions->numSubframes);
        
        u16* palette = &spriteTables->palettes[fd->paletteId * 16];
        
        // Seems like SA3 and KATAM had a different layout?
        u8 oamIndex = (game == SA1 || game == SA2) ? 
            frameDimensions->oamIndex :
        frameDimensions->flip; 
        
        // Pointer to OamData of the whole frame
        OamSplit* frameOamData = (OamSplit*)(&oamDataStart[oamIndex*3]);
        
        int tileSize = (fd->tileIndex & 0x80000000)
            ? TILE_SIZE_8BPP
            : TILE_SIZE_4BPP;
        
        // 'tiles' point to the root of this frame's tiles
        u8* tiles = (fd->tileIndex & 0x80000000)
            ? &spriteTables->tiles_8bpp[fd->tileIndex * tileSize]
            : &spriteTables->tiles_4bpp[fd->tileIndex * tileSize];
        
        u8  tilePitch      = (tileSize / TILE_WIDTH);
        u16 tileImagePitch = (frameDimensions->width / TILE_WIDTH) * tileSize;
        
        const char* fileExt = (tileSize == TILE_SIZE_4BPP) ? "4bpp" : "8bpp";
        sprintf(filenameNoExt, "a%04d_f%03d", animId, frameId);
        sprintf(filePath, "%s/%s.%s",
                framePath, filenameNoExt, fileExt);
        
        u64 arenaReserveLength = (frameDimensions->width*frameDimensions->height)*tileSize;
        if(arenaReserveLength == 0)
            goto skipGeneration;
        
        u8* image = memArenaReserve(fullTileImage, arenaReserveLength);
        
        long fullFrameSize = 0;
        for (int subFrame = 0; subFrame < frameDimensions->numSubframes; subFrame++) {
            // MSVC doesn't support the regular "packed" attribute of GCC.
            // We have to work around that with this cast...
            // Pointer to OamData of each sub-frame
            OamSplit* oamSubFrame = (OamSplit*)&((u16*)frameOamData)[subFrame * 3];
            
            // TODO: Add check for exporting the same data multiple times.
            
            s8Vec2D sizes = sOamTileSizes[oamSubFrame->shape][oamSubFrame->size];
            int numTiles = sizes.x * sizes.y;
            
            s16Vec2D subPos = {oamSubFrame->x, oamSubFrame->y};
            
            u8* subFrameTiles  = &tiles[oamSubFrame->tileNum * tileSize];
            
            for(int y = 0;
                y < sizes.y;
                y++)
            {
                int dstIndex = (subPos.y/TILE_WIDTH + y)*tileImagePitch + subPos.x*tilePitch;
                int srcIndex = (y*sizes.x)*tileSize;
                int subFrameRowSize = sizes.x * tileSize;
                
                //memcpy_s(&image[dstIndex], fullTileImage->size, &subFrameTiles[srcIndex], sizes.x*tileSize);
                if(image)
                    memcpy(&image[dstIndex], &subFrameTiles[srcIndex], sizes.x*tileSize);
                
                fullFrameSize += subFrameRowSize;
            }
            
            fprintf(debugComposition, "(%2d, %2d) => (%3d, %3d)",
                    sizes.x * TILE_WIDTH, sizes.y * TILE_WIDTH,
                    subPos.x, subPos.y);
            
            // Left-bound padding
            if(subFrame + 1 < frameDimensions->numSubframes)
                fprintf(debugComposition, "\n%*s", 25, "");
            
        }
        fprintf(debugComposition, "\n");
        
        skipGeneration:
        if (!wasFrameIndexed(animId, tiles)) {
            indexFrame(tiles);
            
            FILE* frameFile = fopen(filePath, "wb");
            
            if(frameFile) {
                if(image)
                    fwrite(image, fullFrameSize, 1, frameFile);
                
                fclose(frameFile);
            }
            /* Add this file to the output- and tile-generation scripts */
#if 1
            int cmdTileWidth = frameDimensions->width / TILE_WIDTH;
            
            if(frameFile && cmdTileWidth > 0) {
                // Write gbagfx command for conversion script
                fprintf(scriptFilestream, "./gbagfx %s/%s.%s %s/%s.png -object -palette %s/pal_%03d.gbapal -width %d\n",
                        framePath, filenameNoExt, fileExt,
                        framePath, filenameNoExt,
                        palPath, fd->paletteId,
                        cmdTileWidth);
                
                // PNG -> 4BPP script
                // TODO: Split 4bpp and 8bpp into separate files
                fprintf(tile_collection, "./tools/gbagfx/gbagfx %s/%s.png %s/%s.%s -width %d\n",
                        framePath, filenameNoExt,
                        framePath, filenameNoExt, fileExt,
                        cmdTileWidth);
            }
#endif
            
#define ADD_GLOBAL_LABELS_TO_INCBIN FALSE // Can be useful for debugging!
#if ADD_GLOBAL_LABELS_TO_INCBIN
            fprintf(inc_bin,
                    ".global %s\n"
                    "%s:\n", filenameNoExt, filenameNoExt);
#endif // ADD_GLOBAL_LABELS_TO_INCBIN
            
            // Assembly file, putting all tiles together
            fprintf(inc_bin,
                    ".incbin \"%s/%s.%s\"\n",
                    framePath, filenameNoExt, fileExt);
        }
    }
}

void
generateSprites(u8* rom, DynTable* dynTable, SpriteTables* spriteTables, int animMin, int animMax,
                char* framePath, char* docsPath, char* palettePath, char* genFramesScriptFilePath, char* gfxIncFilePath) {
    
    MemArena frameData;
    memArenaInit(&frameData);
    FrameDataInput fdi;
    fdi.data = NULL;
    
    FILE* spriteImagesScript = fopen(gfxIncFilePath, "w");
    fprintf(spriteImagesScript,
            "$(TILES_BUILDDIR)/%%.o: $(TILES_SUBDIR)/%%.s\n"
            "	@echo $(GFX) <flags> -I sound -o $@ $<\n"
            "	@$(AS)");
    
    FILE* tile_script = fopen("obj_tiles_4bpp.sh", "w");
    FILE* incbin = fopen("obj_tiles_4bpp.inc", "w");
    
    FILE* script = fopen(genFramesScriptFilePath, "w");
    fprintf(script, "#!/bin/sh\n");
    
    char debugFilePathBuffer[256];
    sprintf(debugFilePathBuffer, "%s/%s", docsPath, "Debug_FrameComposition.txt");
    FILE* debugFile_FrameComposition = fopen(debugFilePathBuffer, "w");
    fprintf(debugFile_FrameComposition, "--- FRAME COMPOSIITON ---\n");
    fprintf(debugFile_FrameComposition, "FullX, FullY - SubCnt [SubDim, SubPos] \n");
    
    
    // Scratch-memory for one full frame that should be output.
    MemArena fullTileImage;
    memArenaInit(&fullTileImage);
    
    for (int animId = animMin; animId < animMax; animId++) {
        if (spriteTables->animations == 0)
            break;
        
        fdi.frameCount = 0;
        iterateAllCommands(stdout, dynTable, animId, animId + 1, getAnimFrameCount, &fdi.frameCount);
        
        if (fdi.frameCount > 0) {
            fdi.data = memArenaReserve(&frameData, fdi.frameCount * sizeof(FrameData));
            iterateAllCommands(stdout, dynTable, animId, animId + 1, generateFrameData, &fdi);
            
            generateSprite(rom, &fullTileImage, spriteTables, &fdi, debugFile_FrameComposition, script, tile_script, incbin, animId, framePath, docsPath, palettePath);
        }
    }
    
    memArenaFree(&fullTileImage);
    
    fclose(debugFile_FrameComposition);
    fclose(script);
    fclose(tile_script);
    fclose(spriteImagesScript);
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
    
    // TODO: Make this a program parameter
    bool outputC = TRUE;
    
    SpriteTables spriteTables;
    getSpriteTables(rom, game, &spriteTables);
    
    AnimationTable animTable = { 0 };
    animTable.data = spriteTables.animations;
    animTable.entryCount = g_TotalAnimationCount[game];
    
    OutFiles files = { stdout, stdout };
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
    char* gfxIncFilePath          = addToPath(&paths, docsPath, "obj_tiles.inc");
    char* paletteFilePath         = addToPath(&paths, docsPath, "obj_palettes.inc");
    char* genFramesScriptFilePath = addToPath(&paths, docsPath, "gen_frames.sh");
    
    files.header    = fopen(headerFilePath, "w");
    files.animTable = fopen(animationTableFilePath, "w");
#endif
    // For determining multiple writes of the same tiles
    writtenTiles.writtenCount = 0;
    writtenTiles.lastAnim = -1;
    memArenaInit(&writtenTiles.arena);
    
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
    printAnimationDataFile(files.header, &dynTable, &labels, &stringArena, &stringOffsetArena, animTable.entryCount, &files, outputC);
    printAnimationTable(files.animTable, &dynTable, &animTable, &labels, outputC);
#endif
    MemArena tileRanges;
    memArenaInit(&tileRanges);
    
    TileInfo tileInfo = { 0 };
    tileInfo.tileRanges = &tileRanges;
    
#if 0
    generateSprites(rom, &dynTable, &spriteTables, 0, animTable.entryCount,
                    framePath, docsPath, palettePath, genFramesScriptFilePath, gfxIncFilePath);
#endif
    
#define OUTPUT_PALETTES 1
#if OUTPUT_PALETTES
    u16 paletteBuffer[16 * 16];
    int colorsPerPalette = 16;
    int paletteCount = ((spriteTables.tiles_4bpp - (u8*)spriteTables.palettes) / (2*colorsPerPalette));
    
    FILE* paletteInc = fopen(paletteFilePath, "w");
    
    char* filePath = addToPath(&paths, palettePath, "pal_XXXXXX.gbapal");
    char* fileName = lastString(filePath, "pal_");
    // Output every palette as its own file.
    for (int i = 0; i < paletteCount; i++) {
        
        u16* pal = spriteTables.palettes + colorsPerPalette * i;
        
        memcpy(&paletteBuffer, pal, 2 * 16);
        
        sprintf(fileName, "pal_%03d.gbapal", i);
        FILE* palFile = fopen(filePath, "wb");
        fwrite(paletteBuffer, 2, 16, palFile);
        fclose(palFile);
        
        fprintf(paletteInc, "./gbagfx \"palettes/%s\" \"palettes/pal_%03d.pal\"\n", fileName, i);
    }
    fclose(paletteInc);
#endif
    
    
    if(files.animTable && files.animTable != stdout)
        fclose(files.animTable);
    
    if (files.header && files.header != stdout)
        fclose(files.header);
    
    return 0;
}
