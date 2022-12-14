@echo off

REM Debug version - creates a PDB file
cl /Od /Zi animExporter.c ArenaAlloc.c

REM Release version
REM cl /O2 animExporter.c ArenaAlloc.c
