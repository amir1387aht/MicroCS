# Filesystem (modules/fs, `MCS_ENABLE_FS`)

## VFS
```c
mcs_vfs_t vfs; mcs_vfs_init(&vfs);
static mcs_ramfs_t ram; mcs_ramfs_init(&ram, 32 * 1024, NULL, NULL);   /* quota, allocator */
mcs_vfs_mount(&vfs, "/", &mcs_ramfs_ops, &ram, 0);
mcs_vfs_mount(&vfs, "/flash", &mcs_lfs_ops, &lfs, 0);                 /* LittleFS          */
mcs_vfs_mount(&vfs, "/sys", &mcs_ramfs_ops, &sys, MCS_VFS_RDONLY | MCS_VFS_NOEXEC);
mcs_fs_open_lib(vm, &vfs);                                             /* C# File/Directory */
```
* Up to `MCS_VFS_MAX_MOUNTS` (4) mounts; longest prefix wins; mount points appear as
  directories in listings.
* Paths are normalised (`.`/`..`/`//`); `..` cannot climb above `/`, so scripts cannot
  reach host paths outside the mounted roots. Max length `MCS_VFS_PATH_MAX` (128).
* `MCS_VFS_RDONLY` rejects writes (UnauthorizedAccessException); `MCS_VFS_NOEXEC` lets
  scripts read files but `mcs_exec_file`/shell `run` refuse to execute them.
* Rename across mounts returns EINVAL (C# `IOException`).
* Whole-file helpers `mcs_vfs_read_file/write_file` and `mcs_exec_file` (source or image).

## Backends
| Backend | Use | Notes |
|---|---|---|
| `mcs_ramfs_ops` | tests, scratch, MCUs without flash FS | byte quota, custom allocator (e.g. the VM pool), ≤64 entries listed per dir |
| `mcs_posixfs_ops` | host CLI (`--fs DIR`) | confined to the directory; sorted listings |
| `mcs_lfs_ops` | MCU flash, **experimental** | LittleFS v2; `lfs_t` owned by the port; atomic rename; file handles from a fixed table (`MCS_LFS_MAX_FILES`=4, process-wide); LittleFS allocates its per-file cache with `lfs_malloc` unless built with `LFS_NO_MALLOC` |

LittleFS is not bundled. `make lfs-test` downloads v2.9.3 and runs
`tests/c/test_lfs.c` against a RAM block device: C# File/Directory API, remount
persistence ("reboot"), atomic replace via rename, device-full → `IOException` and
space reclamation. It has **not** been run on real NOR/NAND flash or under power-cut
testing; LittleFS's own power-loss guarantees are what the design relies on.

## C# API
`File`: ReadAllText, WriteAllText, AppendAllText, ReadAllLines, WriteAllLines,
AppendAllLines, ReadAllBytes, WriteAllBytes, Exists, Delete, Copy(src, dst[, overwrite]),
Move, GetLength (MicroCS extension; .NET uses `FileInfo.Length`).
`Directory`: Exists, CreateDirectory (recursive), GetFiles, GetDirectories,
GetFileSystemEntries, Delete(path[, recursive]), GetCurrentDirectory (always `/`).
`Path`: Combine, GetFileName, GetExtension, GetFileNameWithoutExtension,
GetDirectoryName, GetFullPath.
Errors map to .NET types and messages: FileNotFoundException,
DirectoryNotFoundException, UnauthorizedAccessException, IOException.
There are no streams (`FileStream`) yet — whole-file operations only (planned).
