@echo off
rem build script

set compiler_flags=-O0 -g -Werror -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -fno-strict-aliasing
set linker_flags=-luser32.lib -lgdi32.lib
set includes=-I%VULKAN_SDK%\Include
set defines=-D_CRT_SECURE_NO_WARNINGS -DSY_DEBUG=1 -DSOFTWARE_RENDERING=0

rem main executable
clang src\main.c %compiler_flags% -std=c17 %linker_flags% %defines% -o bin\sunny.exe

rem renderer dll
clang src\renderer\vulkan_renderer.c %compiler_flags% -std=c17 %defines% %includes% -shared -o bin\vulkan_renderer.dll