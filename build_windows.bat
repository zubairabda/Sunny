@echo off
rem build script

set compiler_flags=-O0 -g -fno-strict-aliasing -Werror -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function
set linker_flags=-luser32.lib -lgdi32.lib -ldinput8.lib
set includes=-I./src/
set defines=-D_CRT_SECURE_NO_WARNINGS -DSY_DEBUG=1 -DSOFTWARE_RENDERING=0

set sources=src/event.c src/cdrom.c src/cpu.c src/debug.c src/disasm.c src/dma.c src/gpu.c src/main.c src/memory.c src/pad.c src/psx.c src/spu.c src/timers.c src/fileio.c src/renderer/renderer.c
set win32_sources=src/audio/win32_audio.c src/input/win32_input.c src/platform/win32_sync.c

rem main executable
clang %sources% %win32_sources% %compiler_flags% -std=c17 %includes% %linker_flags% %defines% -o bin\sunny.exe

rem renderer dll
clang src\renderer\vulkan\win32_vulkan.c %compiler_flags% -std=c17 %includes% -I%VULKAN_SDK%\Include %defines% -shared -o bin\vulkan_renderer.dll
