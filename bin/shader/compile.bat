@echo off

for %%f in (*.vert) do (glslangValidator -V %%f -o %%~nf.spv)
for %%f in (*.frag) do (glslangValidator -V %%f -o %%~nf.spv)

pause