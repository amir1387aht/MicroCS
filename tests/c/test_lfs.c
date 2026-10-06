/* MicroCS + LittleFS integration test (RAM block device, simulated remount).
 * Built by `make lfs-test` (fetches littlefs v2.9.3 into build/third_party). */
#include <stdio.h>
#include <string.h>
#include "mcs.h"
#include "mcs_vfs.h"
#include "lfs.h"
#include "flash_sim.h"

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

/* LittleFS on a simulated SPI chip through mcs_flash_t + mcs_lfs_flash_config */
static void exercise_flash(const char* tag, mcs_flash_part_t* part, unsigned fill_chunks) {
    char what[128], code[1024];
#define T(x) (snprintf(what, sizeof what, "%s: %s", tag, x), what)
    struct lfs_config fc; lfs_t fl;
    CHECK(mcs_lfs_flash_config(&fc, part) == 0, T("mcs_lfs_flash_config"));
    CHECK(lfs_format(&fl, &fc) == 0 && lfs_mount(&fl, &fc) == 0, T("format + mount"));
    mcs_vfs_t v; mcs_vfs_init(&v);
    mcs_vfs_mount(&v, "/f", &mcs_lfs_ops, &fl, 0);
    int r = run(&v, "Directory.CreateDirectory(\"/f/app\"); File.WriteAllText(\"/f/app/main.cs\", \"Console.WriteLine(6*7);\");\n"
                    "File.AppendAllText(\"/f/log\", \"a\"); File.AppendAllText(\"/f/log\", \"b\"); Console.WriteLine(File.ReadAllText(\"/f/log\"));");
    CHECK(r == MCS_OK && !strcmp(g_out, "ab\n"), T("C# File API"));
    lfs_unmount(&fl);
    CHECK(lfs_mount(&fl, &fc) == 0, T("remount"));
    {
        mcs_config_t c; mcs_config_default(&c); c.write_fn = out; c.error_fn = out;
        mcs_vm_t* vm = mcs_new(&c); g_len = 0;
        r = mcs_exec_file(vm, &v, "/f/app/main.cs");
        mcs_free(vm);
        CHECK(r == MCS_OK && !strcmp(g_out, "42\n"), T("persisted script runs"));
    }
    snprintf(code, sizeof code,
        "try { var s = new string('x', 4096); for (int i = 0; i < %u; i++) File.AppendAllText(\"/f/big.bin\", s); Console.WriteLine(\"room\"); }\n"
        "catch (IOException e) { Console.WriteLine(\"full\"); }\nFile.Delete(\"/f/big.bin\");\n"
        "var t = new string('y', 1500); for (int i = 0; i < 300; i++) File.WriteAllText(\"/f/w\" + (i %% 6), t + i);\n"
        "Console.WriteLine(File.ReadAllText(\"/f/w5\").Length);", fill_chunks);
    r = run(&v, code);
    CHECK(r == MCS_OK && !strcmp(g_out, "full\n1503\n"), T("fill -> IOException, delete, churn"));
    if (strcmp(g_out, "full\n1503\n")) printf("got: [%s]\n", g_out);
    lfs_unmount(&fl);
#undef T
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
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

    /* ---- SPI NOR (2 MB, 4 KB sectors) through the generic driver ---- */
    sim_nor_t os; mcs_spinor_t nor; memset(&nor, 0, sizeof nor);
    sim_nor_init(&os, 2u << 20, 0x15, 0x7);
    CHECK(mcs_spinor_init(&nor, sim_nor_xfer, &os, 0, 4096) == 0, "SPI NOR init");
    mcs_flash_part_t np = { &nor.flash, 16, 256 };        /* 1 MB partition at 64 KB */
    exercise_flash("LittleFS on SPI NOR", &np, 300);
    CHECK(os.overwrite_violations == 0 && os.unlocked_writes == 0, "NOR: never programs 0->1");
    printf("NOR: %ld page programs, %ld sector erases\n", os.progs, os.erases);
    free(os.mem);

    /* ---- SPI NAND (64 x 128 KB) with a factory bad block and one that wears out ---- */
    sim_nand_t ns; mcs_spinand_t nand; memset(&nand, 0, sizeof nand);
    sim_nand_init(&ns, 64);
    sim_nand_factory_bad(&ns, 1);
    sim_nand_factory_bad(&ns, 9);
    ns.worn[12] = 1;
    CHECK(mcs_spinand_init(&nand, sim_nand_xfer, &ns, 64) == 0, "SPI NAND init");
    struct lfs_config bad;
    mcs_flash_part_t p0 = { &nand.flash, 0, 0 };
    CHECK(mcs_lfs_flash_config(&bad, &p0) == MCS_VFS_EIO, "NAND: partition with a bad superblock block refused");
    mcs_flash_part_t pp = { &nand.flash, 2, 0 };          /* blocks 2..63 */
    exercise_flash("LittleFS on SPI NAND", &pp, 2200);
    CHECK(ns.nop_violations == 0 && ns.order_violations == 0 && ns.overwrite_violations == 0,
          "NAND: one in-order program per page, never 0->1");
    CHECK(nand.flash.is_bad(&nand.flash, 12) == 1, "NAND: worn-out block marked bad");
    printf("NAND: %ld page programs, %ld block erases, %ld page reads\n", ns.progs, ns.erases, ns.reads);
    free(ns.mem); free(ns.prog_count); free(ns.next_page); free(ns.worn);
    printf("%d/%d checks passed (%d block programs)\n", checks - fails, checks, g_progs);
    return fails ? 1 : 0;
}
