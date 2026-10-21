/* Compile the production backend with only its listing allocations instrumented. */
#include "mcs_vfs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if MCS_ENABLE_FS && (defined(__unix__) || defined(__APPLE__))
#include <unistd.h>
static int checks, failures, fail_at = -1, calls;
static size_t blocks;
#define CHECK(x) do { checks++; if (!(x)) { failures++; fprintf(stderr, "FAIL %d: %s\n", __LINE__, #x); } } while (0)
static void* list_realloc(void* ptr, size_t n) {
    if (calls++ == fail_at) return NULL;
    bool fresh = ptr == NULL;
    void* out = realloc(ptr, n);
    if (out && fresh) blocks++;
    return out;
}
static char* list_strdup(const char* s) {
    if (calls++ == fail_at) return NULL;
    char* out = strdup(s);
    if (out) blocks++;
    return out;
}
static void list_free(void* ptr) {
    if (ptr) { CHECK(blocks > 0); blocks--; free(ptr); }
}
#define realloc(...) list_realloc(__VA_ARGS__)
#define strdup(...) list_strdup(__VA_ARGS__)
#define free(...) list_free(__VA_ARGS__)
#include "../../modules/fs/mcs_vfs_posix.c"
#undef realloc
#undef strdup
#undef free
static int seen, stop_at;
static int entry(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    (void)ud;
    char expected[40]; snprintf(expected, sizeof expected, "item%03d.txt", seen);
    CHECK(!strcmp(name, expected) && !st->is_dir);
    seen++;
    return stop_at && seen == stop_at;
}
int main(void) {
    char root[] = "/tmp/microcs-list-oom-XXXXXX";
    if (!mkdtemp(root)) return 1;
    mcs_posixfs_t fs; CHECK(!mcs_posixfs_init(&fs, root));
    for (int i = 299; i >= 0; i--) {
        char path[256]; snprintf(path, sizeof path, "%s/item%03d.txt", root, i);
        FILE* f = fopen(path, "w"); CHECK(f != NULL); if (f) fclose(f);
    }
    bool complete = false;
    for (int fault = 0; fault < 1024; fault++) {
        fail_at = fault; calls = 0; seen = 0;
        int e = mcs_posixfs_ops.list(&fs, "/", entry, NULL);
        fail_at = -1; CHECK(blocks == 0);
        if (!e) { CHECK(seen == 300); complete = true; break; }
        CHECK(e == MCS_VFS_ENOMEM); CHECK(seen == 0);
    }
    CHECK(complete);
    seen = 0; stop_at = 257;
    CHECK(!mcs_posixfs_ops.list(&fs, "/", entry, NULL)); CHECK(seen == 257); CHECK(blocks == 0);
    CHECK(mcs_posixfs_ops.list(&fs, "/missing", entry, NULL) == MCS_VFS_ENOENT); CHECK(blocks == 0);
    for (int i = 0; i < 300; i++) {
        char path[256]; snprintf(path, sizeof path, "%s/item%03d.txt", root, i); CHECK(!unlink(path));
    }
    CHECK(!rmdir(root));
    printf("%s POSIX list OOM: %d checks, %d failures\n", failures ? "FAIL" : "PASS", checks, failures);
    return failures != 0;
}
#else
int main(void) { puts("SKIP POSIX list OOM: non-POSIX target"); return 0; }
#endif
