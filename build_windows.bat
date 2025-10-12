@echo off
rem build script
set DEBUG=0
IF %DEBUG% == 0 (
    set compiler_flags=-O3 -fno-strict-aliasing -Werror -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-cast-function-type-mismatch
) ELSE (
    set compiler_flags=-O0 -g -fno-strict-aliasing -Werror -Wall -Wextra -Wno-unused-function -Wno-unused-variable -Wno-unused-parameter -Wno-cast-function-type-mismatch
)
set linker_flags=-luser32.lib -lgdi32.lib
set includes=-I./src/
set defines=-D_CRT_SECURE_NO_WARNINGS -DSY_DEBUG=1 -DEXPORT_LIB=1

set sources=src/debug/debug_ui.c src/platform/platform.c src/renderer/renderer.c src/renderer/sw_renderer.c src/allocator.c src/cdrom.c src/config.c src/counters.c src/cpu.c src/debug.c src/disasm.c src/dma.c src/event.c src/gpu.c src/gte.c src/input.c src/main.c src/mdec.c src/memory.c src/psx.c src/savestate.c src/sio.c src/spu.c src/stream.c
set win32_sources=src/platform/win32/win32_platform.c src/platform/win32/audio.c

rem main executable
clang %sources% %win32_sources% %compiler_flags% -std=c17 %includes% %linker_flags% %defines% -o bin\sunny.exe
