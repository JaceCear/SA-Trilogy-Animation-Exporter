# SA-Trilogy-Animation-Exporter
Tool to extract animation data out of any of the Sonic Advance 1-3 ROMs.
I've had this laying around for a while, and I thought it'd be helpful to clean it up a little and push it.

In case you use it for a project, I'd highly appreciate a shoutout! :)

I used the output of this tool for the Sonic Advance 2 decomp by freshollie, which you can find [here](https://github.com/freshollie/sa2).
At the time of writing it's in its `data/animations/` directory.


# Building on Windows
To build using the VS-Compiler, you need to open a "Developer Terminal" (or call vcvarsall.bat) from Visual Studio.
Then you can just call `build.bat` or call
`cl /O2 animExporter.c ArenaAlloc.c`

If you have gcc installed, you can use the Linux command instead!


# Building on UNIX-Systems
Either run `./build.sh`
or build it manually with
`gcc -O2 animExporter.c ArenaAlloc.c -o animExporter`

# Troubleshooting
If your region's ROM does not work, check `getSpriteTables` inside `animExporter.c` to set a different address.
Feel free to add a pull request with a patch case you find a new offset.
