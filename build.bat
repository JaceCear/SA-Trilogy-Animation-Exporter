@echo off

REM Debug version - creates a PDB file
REM cl /Od /Zi animExporter.c ArenaAlloc.c

REM Release version
cl /O2 animExporter.c ArenaAlloc.c
