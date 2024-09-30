@echo off
rem build script

clang makefile.c -O0 -g -std=c17 -o makefile.exe -D_CRT_SECURE_NO_WARNINGS

if %ERRORLEVEL% == 0 (
    makefile
)
