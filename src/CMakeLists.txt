set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(SOURCES event.c cdrom.c cpu.c debug.c disasm.c dma.c gpu.c main.c memory.c pad.c psx.c spu.c timers.c fileio.c renderer/renderer.c)

set(FLAGS)

if(WIN32)
    set(SOURCES ${SOURCES} audio/win32_audio.c input/win32_input.c platform/win32_sync.c)
    set(FLAGS ${FLAGS} WIN32)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

add_executable(sunny ${FLAGS} ${SOURCES})

target_include_directories(sunny PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(sunny PUBLIC -DSY_DEBUG=1)
