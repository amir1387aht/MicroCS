/* MicroCS + YAFFS2 integration test on simulated SPI flash (flash_sim.h):
 * SPI NAND with in-band tags, SPI NAND with spare-area tags (factory and
 * worn-out bad blocks), and SPI NOR. Each runs the C# File/Directory API,
 * a remount, an atomic replace and a fill-to-ENOSPC.
 * Built by `make yaffs-test` (fetches a pinned yaffs2 revision into build/third_party). */
#include <stdio.h>
#include "mcs.h"
#include "mcs_vfs.h"
#include "flash_sim.h"
#include "yaffsfs.h"
#include "yaffs_guts.h"

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

static void exercise(const char* tag, struct yaffs_dev* dev, const char* name, uint32_t fill_kb) {
    char what[128], code[2048];
    mcs_vfs_t vfs; mcs_vfs_init(&vfs);
#define T(s) (snprintf(what, sizeof what, "%s: %s", tag, s), what)
    CHECK(yaffs_mount(name) == 0, T("yaffs_mount (formats a blank device)"));
    CHECK(mcs_vfs_mount(&vfs, "/data", &mcs_yaffs_ops, dev, 0) == 0, T("mount at /data"));
    int r = run(&vfs,
        "Directory.CreateDirectory(\"/data/app\");\n"
        "File.WriteAllText(\"/data/app/main.cs\", \"Console.WriteLine(6*7);\");\n"
        "File.AppendAllText(\"/data/log.txt\", \"a\"); File.AppendAllText(\"/data/log.txt\", \"b\");\n"
        "File.WriteAllLines(\"/data/app/cfg.txt\", new[]{\"x=1\",\"y=2\"});\n"
        "Console.WriteLine(File.ReadAllText(\"/data/log.txt\") + \" \" + File.ReadAllLines(\"/data/app/cfg.txt\").Length);\n"
        "var fs = Directory.GetFiles(\"/data/app\"); Array.Sort(fs); Console.WriteLine(string.Join(\",\", fs));\n"
        "File.Move(\"/data/app/cfg.txt\", \"/data/app/cfg2.txt\");\n"
        "Console.WriteLine(File.Exists(\"/data/app/cfg.txt\") + \" \" + File.Exists(\"/data/app/cfg2.txt\"));\n"
        "try { File.ReadAllText(\"/data/nope.txt\"); } catch (FileNotFoundException e) { Console.WriteLine(\"FNF\"); }\n"
        "try { Directory.Delete(\"/data/app\"); } catch (IOException e) { Console.WriteLine(\"not empty\"); }\n"
        "Console.WriteLine(Directory.Exists(\"/data/app\") + \" \" + Directory.GetDirectories(\"/data\").Length);\n");
    const char* want = "ab 2\n/data/app/cfg.txt,/data/app/main.cs\nFalse True\nFNF\nnot empty\nTrue 1\n";
    CHECK(r == MCS_OK && !strcmp(g_out, want), T("C# File/Directory API"));
    if (strcmp(g_out, want)) printf("got: [%s]\n", g_out);

    CHECK(yaffs_unmount(name) == 0 && yaffs_mount(name) == 0, T("unmount + remount ('reboot')"));
    {
        mcs_config_t c; mcs_config_default(&c); c.write_fn = out; c.error_fn = out;
        mcs_vm_t* vm = mcs_new(&c); g_len = 0;
        r = mcs_exec_file(vm, &vfs, "/data/app/main.cs");
        mcs_free(vm);
        CHECK(r == MCS_OK && !strcmp(g_out, "42\n"), T("script persisted and runs via mcs_exec_file"));
    }
    CHECK(mcs_vfs_write_file(&vfs, "/data/app/main.cs.part", "Console.WriteLine(1);", 21, false) == 0 &&
          mcs_vfs_rename(&vfs, "/data/app/main.cs.part", "/data/app/main.cs") == 0, T("rename .part over existing file"));
    mcs_vfs_stat_t st;
    CHECK(mcs_vfs_stat(&vfs, "/data/app/main.cs", &st) == 0 && st.size == 21 && !st.is_dir, T("stat after replace"));
    CHECK(mcs_vfs_stat(&vfs, "/data/app/main.cs.part", &st) == MCS_VFS_ENOENT, T(".part file gone"));
    snprintf(code, sizeof code,
        "try { var s = new string('x', 4096); for (int i = 0; i < %u; i++) File.AppendAllText(\"/data/big.bin\", s); Console.WriteLine(\"room\"); }\n"
        "catch (IOException e) { Console.WriteLine(\"full\"); }\nFile.Delete(\"/data/big.bin\"); Console.WriteLine(File.Exists(\"/data/big.bin\"));\n"
        "File.WriteAllText(\"/data/after.txt\", \"ok\"); Console.WriteLine(File.ReadAllText(\"/data/after.txt\"));", fill_kb / 4);
    r = run(&vfs, code);
    CHECK(r == MCS_OK && !strcmp(g_out, "full\nFalse\nok\n"), T("device full -> IOException, space reclaimed after delete"));
    if (strcmp(g_out, "full\nFalse\nok\n")) printf("got: [%s]\n", g_out);
    CHECK(yaffs_unmount(name) == 0, T("unmount"));
}

int main(void) {
    /* ---- SPI NAND, 128 blocks x 128 KB (16 MB), two partitions ---- */
    sim_nand_t ns; mcs_spinand_t nand; memset(&nand, 0, sizeof nand);
    sim_nand_init(&ns, 128);
    sim_nand_factory_bad(&ns, 3);
    sim_nand_factory_bad(&ns, 70);
    CHECK(mcs_spinand_init(&nand, sim_nand_xfer, &ns, 128) == 0, "SPI NAND init");

    static struct yaffs_dev d1, d2, d3;
    mcs_flash_part_t p1 = { &nand.flash, 0, 64 }, p2 = { &nand.flash, 64, 64 };
    CHECK(mcs_yaffs_flash_dev(&d1, &p1, "/nand") == 0 && d1.param.inband_tags == 1, "NAND partition 1: in-band tags");
    CHECK(mcs_yaffs_flash_dev(&d2, &p2, "/nand2") == 0, "NAND partition 2");
    d2.param.inband_tags = 0;                         /* packed tags in the spare area */
    exercise("NAND in-band", &d1, "/nand", 9000);
    exercise("NAND spare tags", &d2, "/nand2", 9000);
    /* a block wears out while in use: YAFFS retires it and keeps going */
    ns.worn[100] = 1;
    CHECK(yaffs_mount("/nand2") == 0, "NAND: remount with a worn-out block");
    {
        mcs_vfs_t vfs; mcs_vfs_init(&vfs); mcs_vfs_mount(&vfs, "/data", &mcs_yaffs_ops, &d2, 0);
        int r = run(&vfs, "var s = new string('y', 2000); for (int i = 0; i < 6000; i++) File.WriteAllText(\"/data/w\" + (i % 8) + \".txt\", s + i);\n"
                          "Console.WriteLine(File.ReadAllText(\"/data/w7.txt\").Length);");
        CHECK(r == MCS_OK && !strcmp(g_out, "2004\n"), "NAND: churn across a worn-out block");
        if (strcmp(g_out, "2004\n")) printf("got: [%s]\n", g_out);
        yaffs_unmount("/nand2");
    }
    CHECK(ns.mem[(size_t)100 * SN_PPB * (SN_PAGE + SN_SPARE) + SN_PAGE] == 0x00, "NAND: worn-out block retired (bad-block marker written)");
    printf("NAND: blocks marked bad by YAFFS: %u\n", (unsigned)(d1.n_bad_markings + d2.n_bad_markings));
    CHECK(ns.nop_violations == 0 && ns.order_violations == 0 && ns.overwrite_violations == 0,
          "NAND: one in-order program per page, never 0->1");
    printf("NAND violations: nop %ld order %ld overwrite %ld\n", ns.nop_violations, ns.order_violations, ns.overwrite_violations);
    printf("NAND: %ld page programs, %ld block erases, %ld page reads\n", ns.progs, ns.erases, ns.reads);

    /* ---- SPI NOR, 2 MB, 4 KB sectors (512-byte chunks, in-band tags) ---- */
    sim_nor_t os; mcs_spinor_t nor; memset(&nor, 0, sizeof nor);
    sim_nor_init(&os, 2u << 20, 0x15, 0);
    CHECK(mcs_spinor_init(&nor, sim_nor_xfer, &os, 0, 4096) == 0, "SPI NOR init");
    mcs_flash_part_t p3 = { &nor.flash, 0, 0 };
    CHECK(mcs_yaffs_flash_dev(&d3, &p3, "/nor") == 0 && d3.param.inband_tags && d3.param.chunks_per_block == 8, "NOR: 512-byte chunks, in-band tags");
    exercise("NOR", &d3, "/nor", 2100);
    CHECK(os.overwrite_violations == 0, "NOR: never programs 0->1");
    printf("NOR: %ld programs, %ld sector erases\n", os.progs, os.erases);
    free(os.mem);

    free(ns.mem); free(ns.prog_count); free(ns.next_page); free(ns.worn);
    unsigned cur, hw; yaffsfs_get_malloc_values(&cur, &hw);
    printf("yaffs heap high-water: %u bytes\n", hw);
    printf("%d/%d checks passed\n", checks - fails, checks);
    return fails ? 1 : 0;
}
