#define BUILDKIT_IMPLEMENTATION
#include "buildkit.h"

#include <time.h>

int main(int argc, char **argv)
{
    clock_t begin = clock();

    BuildRule cc_rule = build_rule("clang -c {in} -I./src -D_CRT_SECURE_NO_WARNINGS -DSY_DEBUG=1 -O0 -g -std=c17 -fno-strict-aliasing -Werror -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable -Wno-unused-function -Wno-cast-function-type-mismatch -o {out}", ".o");
    BuildRule output_rule = build_rule("clang {in} -o {out} -g -luser32.lib -lgdi32.lib -ldinput8.lib", ".exe");

    StringArray *include_paths = string_array_alloc(1024);
    string_array_push(include_paths, "src");

    BuildOptions opt = {0};
    opt.cc_rule = cc_rule;
    opt.output_rule = output_rule;
    opt.dependency_kind = DEPS_KIND_SCAN;
    opt.output_dir = "bin";
    opt.include_paths = include_paths;
    opt.generate_compile_commands = 1;

    StringArray *sources = string_array_alloc(65536);
    add_files(sources, "src/*.c");
    add_files(sources, "src/debug/*.c");
    add_files(sources, "src/platform/*.c");
    add_files(sources, "src/renderer/*.c");

    HostOSType host = get_host_os();
    if (host == OS_WINDOWS)
    {
        add_files(sources, "src/platform/win32/*.c");
    }

    build_target("sunny", sources, &opt);

    clock_t end = clock();
    clock_t elapsed = end - begin;
    printf("build time: %.2fs\n", (float)elapsed / CLOCKS_PER_SEC);

    return 0;
}
