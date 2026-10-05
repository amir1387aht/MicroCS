/* MicroCS + LittleFS integration test (RAM block device, simulated remount).
 * Built by `make lfs-test` (fetches littlefs v2.9.3 into build/third_party). */
#include <stdio.h>
#include <string.h>
#include "mcs.h"
#include "mcs_vfs.h"
#include "lfs.h"

#define BLOCK 512
#define NBLOCKS 256
static uint8_t g_flash[BLOCK * NBLOCKS];
static int g_progs;
static int bd_read(const struct lfs_config* c, lfs_block_t b, lfs_off_t o, void* buf, lfs_size_t n) { (void)c; memcpy(buf, g_flash + b * BLOCK + o, n); return 0; }
static int bd_prog(const struct lfs_config* c, lfs_block_t b, lfs_off_t o, const void* buf, lfs_size_t n) { (void)c; memcpy(g_flash + b * BLOCK + o, buf, n); g_progs++; return 0; }
static int bd_erase(const struct lfs_config* c, lfs_block_t b) { (void)c; memset(g_flash + b * BLOCK, 0xff, BLOCK); return 0; }
static int bd_sync(const struct lfs_config* c) { (void)c; return 0; }
static const struct lfs_config cfg = {
    .read = bd_read, .prog = bd_prog, .erase = bd_erase, .sync = bd_sync,
    .read_size = 16, .prog_size = 16, .block_size = BLOCK, .block_count = NBLOCKS,
    .cache_size = 64, .lookahead_size = 16, .block_cycles = 500,
};

static char g_out[4096]; static size_t g_len;
static void out(void* ud, const char* s, size_t n) { (void)ud; if (g_len + n < sizeof g_out) { memcpy(g_out + g_len, s, n); g_len += n; g_out[g_len] = 0; } }
static int fails, checks;
#define CHECK(c, what) do { checks++; if (!(c)) { fails++; printf("FAIL %s\n", what); } else printf("PASS %s\n", what); } while (0)

static int run(mcs_vfs_t* vfs, const char* code) {
    mcs_config_t c; mcs_config_default(&c); c.write_fn = out; c.error_fn = out;
    mcs_vm_t* vm = mcs_new(&c);
    mcs_fs_open_lib(vm, vfs);
    g_len = 0; g_out[0] = 0;
    int r = mcs_exec_source(vm, "t.cs", code);
    mcs_free(vm);
    return r;
}

int main(void) {
    lfs_t lfs;
    memset(g_flash, 0xff, sizeof g_flash);
    CHECK(lfs_format(&lfs, &cfg) == 0 && lfs_mount(&lfs, &cfg) == 0, "format + mount LittleFS on RAM block device");
    mcs_vfs_t vfs; mcs_vfs_init(&vfs);
    CHECK(mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0) == 0, "mount at /flash");

    int r = run(&vfs,
        "Directory.CreateDirectory(\"/flash/app\");\n"
        "File.WriteAllText(\"/flash/app/main.cs\", \"Console.WriteLine(6*7);\");\n"
        "File.AppendAllText(\"/flash/log.txt\", \"a\"); File.AppendAllText(\"/flash/log.txt\", \"b\");\n"
        "File.WriteAllLines(\"/flash/app/cfg.txt\", new[]{\"x=1\",\"y=2\"});\n"
        "Console.WriteLine(File.ReadAllText(\"/flash/log.txt\") + \" \" + File.ReadAllLines(\"/flash/app/cfg.txt\").Length);\n"
        "Console.WriteLine(string.Join(\",\", Directory.GetFiles(\"/flash/app\")));\n"
        "File.Move(\"/flash/app/cfg.txt\", \"/flash/app/cfg2.txt\");\n"
        "Console.WriteLine(File.Exists(\"/flash/app/cfg.txt\") + \" \" + File.Exists(\"/flash/app/cfg2.txt\"));\n"
        "try { File.ReadAllText(\"/flash/nope.txt\"); } catch (FileNotFoundException e) { Console.WriteLine(\"FNF\"); }\n"
        "try { Directory.Delete(\"/flash/app\"); } catch (IOException e) { Console.WriteLine(\"not empty\"); }\n");
    CHECK(r == MCS_OK, "C# File/Directory API on LittleFS");
    CHECK(!strcmp(g_out, "ab 2\n/flash/app/cfg.txt,/flash/app/main.cs\nFalse True\nFNF\nnot empty\n"), "expected script output");
    if (strcmp(g_out, "ab 2\n/flash/app/cfg.txt,/flash/app/main.cs\nFalse True\nFNF\nnot empty\n")) printf("got: [%s]\n", g_out);

    /* simulate reboot: unmount + remount from the same flash image */
    lfs_unmount(&lfs);
    CHECK(lfs_mount(&lfs, &cfg) == 0, "remount after 'reboot'");
    {
        mcs_config_t c; mcs_config_default(&c); c.write_fn = out; c.error_fn = out;
        mcs_vm_t* vm = mcs_new(&c); g_len = 0;
        r = mcs_exec_file(vm, &vfs, "/flash/app/main.cs");
        mcs_free(vm);
        CHECK(r == MCS_OK && !strcmp(g_out, "42\n"), "script persisted across remount and runs via mcs_exec_file");
    }
    /* atomic replace: rename onto an existing file */
    CHECK(mcs_vfs_write_file(&vfs, "/flash/app/main.cs.part", "Console.WriteLine(1);", 21, false) == 0 &&
          mcs_vfs_rename(&vfs, "/flash/app/main.cs.part", "/flash/app/main.cs") == 0, "rename .part over existing file (atomic on LittleFS)");
    mcs_vfs_stat_t st;
    CHECK(mcs_vfs_stat(&vfs, "/flash/app/main.cs", &st) == 0 && st.size == 21 && !st.is_dir, "stat after replace");
    CHECK(mcs_vfs_stat(&vfs, "/flash/app/main.cs.part", &st) == MCS_VFS_ENOENT, ".part file gone");
    /* fill the device -> ENOSPC surfaces as IOException */
    r = run(&vfs, "try { var s = new string('x', 4096); for (int i = 0; i < 100; i++) File.AppendAllText(\"/flash/big.bin\", s); }\n"
                  "catch (IOException e) { Console.WriteLine(\"full\"); }\nFile.Delete(\"/flash/big.bin\"); Console.WriteLine(File.Exists(\"/flash/big.bin\"));");
    CHECK(r == MCS_OK && !strcmp(g_out, "full\nFalse\n"), "device full -> IOException, space reclaimed after delete");
    if (strcmp(g_out, "full\nFalse\n")) printf("got: [%s]\n", g_out);
    lfs_unmount(&lfs);
    printf("%d/%d checks passed (%d block programs)\n", checks - fails, checks, g_progs);
    return fails ? 1 : 0;
}
