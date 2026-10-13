#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mcs.h"
static void quiet(void* ud, const char* text, size_t len) { (void)ud; (void)text; (void)len; }
int main(void) {
    char source[20000]; strcpy(source, "int total = 0;\n");
    for (int i = 0; i < 1200; i++) strcat(source, "total += 1;\n");
    strcat(source, "Console.WriteLine(total);\n");
    for (int i = 0; i < 1000; i++) {
        mcs_config_t cfg; mcs_config_default(&cfg); cfg.write_fn = quiet;
        mcs_vm_t* vm = mcs_new(&cfg); if (!vm) return 1;
        uint8_t* image; size_t length;
        if (mcs_compile_image_ex(vm, "compiler-profile.cs", source, MCS_IMAGE_NO_OPT, &image, &length) != MCS_OK) { fprintf(stderr, "%s\n", mcs_last_error(vm)); return 1; }
        mcs_free_image(vm, image); mcs_free(vm);
    }
    puts("PASS 1000 successful compile-only repetitions, 1200 statements each");
    return 0;
}
