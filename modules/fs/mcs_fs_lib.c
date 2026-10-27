/* MicroCS - C# System.IO subset (File, Directory, Path, DriveInfo) on top of the VFS.
 * There is no current directory: relative paths are taken from "/". */
#include "mcs_vfs.h"
#if MCS_ENABLE_FS
#include <string.h>
#include <stdio.h>
#include "mcs_native_internal.h"

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#define NATIVE(name) static mcs_value_t name(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv)
#define VFS() ((mcs_vfs_t*)mcs_get_ext(vm, MCS_EXT_VFS))
#define ARG_PATH(i, var) const char* var = mcs_to_cstr(vm, argv[i]); if (mcs_has_exception(vm)) return mcs_null()

/* .NET-style exception for a VFS error on `path` */
static mcs_value_t io_fail(mcs_vm_t* vm, int err, const char* path, bool is_dir) {
    switch (err) {
    case MCS_VFS_ENOENT:
        if (is_dir) mcs_raise(vm, "DirectoryNotFoundException", "Could not find a part of the path '%s'.", path);
        else mcs_raise(vm, "FileNotFoundException", "Could not find file '%s'.", path);
        break;
    case MCS_VFS_EACCES: mcs_raise(vm, "UnauthorizedAccessException", "Access to the path '%s' is denied.", path); break;
    default: mcs_raise(vm, "IOException", "%s: '%s'", mcs_vfs_strerror(err), path); break;
    }
    return mcs_null();
}

static bool is_file(mcs_vfs_t* vfs, const char* p) { mcs_vfs_stat_t st; return !mcs_vfs_stat(vfs, p, &st) && !st.is_dir; }
static bool is_dir(mcs_vfs_t* vfs, const char* p) { mcs_vfs_stat_t st; return !mcs_vfs_stat(vfs, p, &st) && st.is_dir; }

/* -------------------------------------------------------------- File */
typedef struct {
    mcs_vm_t* vm;
    const char* data;
    size_t len;
    int kind;                         /* 0 text, 1 lines, 2 bytes */
    mcs_value_t result;
} read_result_t;
static void make_read_result(void* ud) {
    read_result_t* c = (read_result_t*)ud;
    mcs_vm_t* vm = c->vm;
    if (!c->kind) { c->result = mcs_string_n(vm, c->data, c->len); return; }
    if (c->kind == 2) {
        c->result = mcs_new_array(vm, (uint32_t)c->len);
        for (size_t i = 0; i < c->len; i++) mcs_set_index(c->result, (uint32_t)i, mcs_int((uint8_t)c->data[i]));
        return;
    }
    uint32_t n = 0;
    for (size_t i = 0; i < c->len; i++) if (c->data[i] == '\n') n++;
    if (c->len && c->data[c->len - 1] != '\n') n++;
    c->result = mcs_new_array(vm, n);
    size_t start = 0; uint32_t k = 0;
    for (size_t i = 0; i <= c->len && k < n; i++) {
        if (i == c->len || c->data[i] == '\n') {
            size_t end = i;
            if (end > start && c->data[end - 1] == '\r') end--;
            mcs_set_index(c->result, k++, mcs_string_n(vm, c->data + start, end - start));
            start = i + 1;
        }
    }
}
static mcs_value_t read_all(mcs_vm_t* vm, mcs_value_t* argv, int kind) {
    ARG_PATH(0, p);
    char* data; size_t len;
    mcs_vfs_t* vfs = VFS();
    int e = mcs_vfs_read_file(vfs, p, &data, &len);
    if (e) return io_fail(vm, e, p, false);
    read_result_t c = { vm, data, len, kind, mcs_null() };
    int code = mcs_protect_native(vm, make_read_result, &c);
    mcs_vfs_free(vfs, data, len);
    mcs_rethrow_native(vm, code);
    return c.result;
}
NATIVE(f_read_text) { return read_all(vm, argv, 0); }

static mcs_value_t write_text(mcs_vm_t* vm, mcs_value_t* argv, bool append) {
    ARG_PATH(0, p);
    mcs_value_t s = mcs_tostring(vm, argv[1]);
    int e = mcs_vfs_write_file(VFS(), p, mcs_cstr(s), mcs_strlen(s), append);
    return e ? io_fail(vm, e, p, false) : mcs_null();
}
NATIVE(f_write_text) { return write_text(vm, argv, false); }
NATIVE(f_append_text) { return write_text(vm, argv, true); }

NATIVE(f_read_lines) { return read_all(vm, argv, 1); }

typedef struct {
    mcs_vm_t* vm;
    mcs_vfs_file_t* file;
    mcs_value_t seq;
    bool bytes;
    int error;
} write_seq_t;
static void write_seq_body(void* ud) {
    write_seq_t* c = (write_seq_t*)ud;
    mcs_vm_t* vm = c->vm;
    uint32_t n = mcs_len(c->seq);
    uint8_t buf[64]; size_t bl = 0;
    for (uint32_t i = 0; i < n && !c->error; i++) {
        mcs_value_t v = mcs_index(c->seq, i);
        if (c->bytes) {
            buf[bl++] = (uint8_t)mcs_to_int(vm, v);
            if (bl == sizeof buf) { if (mcs_vfs_write(c->file, buf, bl) != (int)bl) c->error = MCS_VFS_EIO; bl = 0; }
        } else {
            mcs_value_t s = mcs_tostring(vm, v);
            if (mcs_has_exception(vm)) break;
            size_t sl = mcs_strlen(s);
            if (mcs_vfs_write(c->file, mcs_cstr(s), sl) != (int)sl || mcs_vfs_write(c->file, "\n", 1) != 1) c->error = MCS_VFS_EIO;
        }
        if (mcs_has_exception(vm)) break;
    }
    if (!c->error && bl && mcs_vfs_write(c->file, buf, bl) != (int)bl) c->error = MCS_VFS_EIO;
}
static int write_seq(mcs_vm_t* vm, mcs_vfs_t* vfs, const char* p, mcs_value_t seq, bool append, bool bytes) {
    mcs_vfs_file_t f;
    int e = mcs_vfs_open(vfs, p, append ? MCS_VFS_APPEND : MCS_VFS_WRITE, &f);
    if (e) return e;
    write_seq_t c = { vm, &f, seq, bytes, 0 };
    int code = mcs_protect_native(vm, write_seq_body, &c);
    int closed = mcs_vfs_close(&f);
    mcs_rethrow_native(vm, code);
    return c.error ? c.error : closed;
}

static bool check_seq(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_null(v)) { mcs_raise(vm, "ArgumentNullException", "Value cannot be null."); return false; }
    int k = mcs_obj_kind(v);
    if (k != MCS_O_ARRAY && k != MCS_O_LIST) { mcs_raise(vm, "ArgumentException", "expected an array or List"); return false; }
    return true;
}

NATIVE(f_write_lines) {
    ARG_PATH(0, p);
    if (!check_seq(vm, argv[1])) return mcs_null();
    int e = write_seq(vm, VFS(), p, argv[1], false, false);
    return e && !mcs_has_exception(vm) ? io_fail(vm, e, p, false) : mcs_null();
}
NATIVE(f_append_lines) {
    ARG_PATH(0, p);
    if (!check_seq(vm, argv[1])) return mcs_null();
    int e = write_seq(vm, VFS(), p, argv[1], true, false);
    return e && !mcs_has_exception(vm) ? io_fail(vm, e, p, false) : mcs_null();
}
NATIVE(f_read_bytes) { return read_all(vm, argv, 2); }
NATIVE(f_write_bytes) {
    ARG_PATH(0, p);
    if (!check_seq(vm, argv[1])) return mcs_null();
    int e = write_seq(vm, VFS(), p, argv[1], false, true);
    return e && !mcs_has_exception(vm) ? io_fail(vm, e, p, false) : mcs_null();
}
NATIVE(f_exists) {
    const char* p = mcs_cstr(argv[0]);
    return mcs_bool(p && is_file(VFS(), p));
}
NATIVE(f_delete) {
    ARG_PATH(0, p);
    if (is_dir(VFS(), p)) return mcs_raise(vm, "UnauthorizedAccessException", "Access to the path '%s' is denied.", p), mcs_null();
    int e = mcs_vfs_remove(VFS(), p);
    return e && e != MCS_VFS_ENOENT ? io_fail(vm, e, p, false) : mcs_null();   /* .NET: missing file is not an error */
}
NATIVE(f_copy) {
    ARG_PATH(0, a);
    ARG_PATH(1, b);
    bool overwrite = argc > 2 && mcs_truthy(argv[2]);
    if (!overwrite && is_file(VFS(), b)) { mcs_raise(vm, "IOException", "The file '%s' already exists.", b); return mcs_null(); }
    char* data; size_t len;
    int e = mcs_vfs_read_file(VFS(), a, &data, &len);
    if (e) return io_fail(vm, e, a, false);
    e = mcs_vfs_write_file(VFS(), b, data, len, false);
    mcs_vfs_free(VFS(), data, len);
    return e ? io_fail(vm, e, b, false) : mcs_null();
}
NATIVE(f_move) {
    ARG_PATH(0, a);
    ARG_PATH(1, b);
    if (!is_file(VFS(), a)) return io_fail(vm, MCS_VFS_ENOENT, a, false);
    int e = mcs_vfs_rename(VFS(), a, b);
    if (e == MCS_VFS_EEXIST) { mcs_raise(vm, "IOException", "Cannot create a file when that file already exists."); return mcs_null(); }
    if (e == MCS_VFS_EINVAL) {  /* across mounts: copy + delete */
        char* data; size_t len;
        if ((e = mcs_vfs_read_file(VFS(), a, &data, &len))) return io_fail(vm, e, a, false);
        e = mcs_vfs_write_file(VFS(), b, data, len, false);
        mcs_vfs_free(VFS(), data, len);
        if (!e) e = mcs_vfs_remove(VFS(), a);
    }
    return e ? io_fail(vm, e, a, false) : mcs_null();
}
NATIVE(f_length) {
    ARG_PATH(0, p);
    mcs_vfs_stat_t st;
    int e = mcs_vfs_stat(VFS(), p, &st);
    if (e || st.is_dir) return io_fail(vm, e ? e : MCS_VFS_ENOENT, p, false);
    return mcs_int((mcs_int_t)st.size);
}

#if MCS_ENABLE_FILE_STREAMS
/* FileStream: one small native handle, caller-owned byte[] and fixed 64-byte
 * transfer scratch. No file-sized temporary allocations or background work. */
typedef struct {
    mcs_vfs_file_t file;
    char path[MCS_VFS_PATH_MAX];
    int32_t position, length, append_start;
    uint8_t access, opened, append, last_op;
} file_stream_t;
static const mcs_class_def_t stream_def;
static void stream_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    file_stream_t* s = (file_stream_t*)mcs_userdata(self);
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "FileStream(path, FileMode[, FileAccess]) expected"); return; }
    const char* path = mcs_cstr(argv[0]);
    if (!path) { mcs_raise(vm, mcs_is_null(argv[0]) ? "ArgumentNullException" : "ArgumentException", "File path must be a string"); return; }
    if (!*path) { mcs_raise(vm, "ArgumentException", "File path cannot be empty"); return; }
    mcs_int_t mode = mcs_to_int(vm, argv[1]);
    mcs_int_t access = argc == 3 ? mcs_to_int(vm, argv[2]) : mode == 6 ? 2 : 3;
    if (mcs_has_exception(vm)) return;
    if (mode < 1 || mode > 6 || access < 1 || access > 3 || (mode != 3 && !(access & 2)) || (mode == 6 && access != 2)) {
        mcs_raise(vm, "ArgumentException", "Invalid FileMode/FileAccess combination"); return;
    }
    int e = mcs_vfs_normalize(path, s->path, sizeof s->path);
    if (e) { io_fail(vm, e, path, false); return; }
    mcs_vfs_t* vfs = VFS();
    if (!vfs) { mcs_raise(vm, "IOException", "No filesystem mounted"); return; }
    mcs_vfs_stat_t st;
    e = mcs_vfs_stat(vfs, s->path, &st);
    if (e && e != MCS_VFS_ENOENT) { io_fail(vm, e, s->path, false); return; }
    if (!e && st.is_dir) { io_fail(vm, MCS_VFS_EACCES, s->path, false); return; }
    if (!e && st.size > 0x7fffffff) { mcs_raise(vm, "IOException", "FileStream supports files up to 2 GiB - 1 byte"); return; }
    int flags = (access & 1) ? MCS_VFS_READ : 0;
    if (access & 2) flags |= MCS_VFS_UPDATE;
    switch (mode) {
    case 1: flags |= MCS_VFS_CREATE | MCS_VFS_EXCL; break;
    case 2: flags |= MCS_VFS_WRITE; break;
    case 4: flags |= MCS_VFS_CREATE; break;
    case 5: flags |= MCS_VFS_TRUNC; break;
    case 6: flags |= MCS_VFS_APPEND; break;
    }
    e = mcs_vfs_open(vfs, s->path, flags, &s->file);
    if (e) { io_fail(vm, e, s->path, false); return; }
    s->opened = 1; s->access = (uint8_t)access;
    s->length = (!e && mode != 1 && mode != 2 && mode != 5 && mcs_vfs_stat(vfs, s->path, &st) == 0) ? (int32_t)st.size : 0;
    if (mode == 6) { s->append = 1; s->append_start = s->length; s->position = s->length; }
}
static void stream_free(mcs_vm_t* vm, void* data) {
    (void)vm; file_stream_t* s = (file_stream_t*)data;
    if (s->opened) { s->opened = 0; (void)mcs_vfs_close(&s->file); }
}
#define STREAM() file_stream_t* s = (file_stream_t*)mcs_check_userdata(vm, self, &stream_def); \
    if (!s) return mcs_null(); \
    if (!s->opened) { mcs_raise(vm, "ObjectDisposedException", "FileStream"); return mcs_null(); }
static bool stream_access(mcs_vm_t* vm, file_stream_t* s, int access) {
    if (!(s->access & access)) { mcs_raise(vm, "NotSupportedException", access == 1 ? "Stream is not readable" : "Stream is not writable"); return false; }
    if (s->last_op && s->last_op != access && s->file.ops->seek) {
        int32_t p;
        int e = mcs_vfs_seek(&s->file, 0, 1, &p);
        if (e) { io_fail(vm, e, s->path, false); return false; }
        s->position = p;
    }
    s->last_op = (uint8_t)access;
    return true;
}
static bool stream_slice(mcs_vm_t* vm, int argc, mcs_value_t* a, uint32_t* off, uint32_t* count) {
    if (argc != 3 || mcs_obj_kind(a[0]) != MCS_O_ARRAY) { mcs_raise(vm, "ArgumentException", "Expected (byte[] buffer, int offset, int count)"); return false; }
    mcs_int_t o = mcs_to_int(vm, a[1]), n = mcs_to_int(vm, a[2]);
    if (mcs_has_exception(vm)) return false;
    uint32_t len = mcs_len(a[0]);
    if (o < 0 || n < 0 || (uint64_t)o > len || (uint64_t)n > len - (uint64_t)o) { mcs_raise(vm, "ArgumentOutOfRangeException", "Buffer offset/count out of range"); return false; }
    *off = (uint32_t)o; *count = (uint32_t)n; return true;
}
NATIVE(fs_read) {
    STREAM(); uint32_t off, count;
    if (!stream_slice(vm, argc, argv, &off, &count) || !stream_access(vm, s, 1)) return mcs_null();
    uint8_t buf[64]; uint32_t done = 0;
    while (done < count) {
        size_t n = count - done; if (n > sizeof buf) n = sizeof buf;
        if (n > 0x7fffffffu - (uint32_t)s->position) n = 0x7fffffffu - (uint32_t)s->position;
        if (!n) break; /* Maximum supported position is also EOF. */
        int r = mcs_vfs_read(&s->file, buf, n);
        if (r < 0) return io_fail(vm, r, s->path, false);
        if ((size_t)r > n) return io_fail(vm, MCS_VFS_EIO, s->path, false);
        if (!r) break;
        for (int i = 0; i < r; i++) mcs_set_index(argv[0], off + done + (uint32_t)i, mcs_int(buf[i]));
        done += (uint32_t)r; s->position += r;
        if ((size_t)r < n) break; /* .NET Read may return fewer bytes than requested. */
    }
    return mcs_int((mcs_int_t)done);
}
static bool stream_written(mcs_vm_t* vm, file_stream_t* s) {
    /* O_APPEND backends write at EOF even after a forward seek. Report the
     * backend's actual position, not the stale requested offset. */
    if (s->append && s->file.ops->seek) {
        int32_t p; int e = mcs_vfs_seek(&s->file, 0, 1, &p);
        if (e) { io_fail(vm, e, s->path, false); return false; }
        s->position = p;
    }
    if (s->position > s->length) s->length = s->position;
    return true;
}
NATIVE(fs_write) {
    STREAM(); uint32_t off, count;
    if (!stream_slice(vm, argc, argv, &off, &count) || !stream_access(vm, s, 2)) return mcs_null();
    if (count > 0x7fffffffu - (uint32_t)s->position) { mcs_raise(vm, "IOException", "Stream exceeds position limit"); return mcs_null(); }
    /* Validate before writing any data, so a bad byte cannot partially write a file. */
    for (uint32_t i = 0; i < count; i++) {
        mcs_int_t v = mcs_to_int(vm, mcs_index(argv[0], off + i));
        if (mcs_has_exception(vm)) return mcs_null();
        if (v < 0 || v > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "Byte value outside 0..255"); return mcs_null(); }
    }
    uint8_t buf[64]; uint32_t done = 0;
    while (done < count) {
        size_t n = count - done; if (n > sizeof buf) n = sizeof buf;
        for (size_t i = 0; i < n; i++) buf[i] = (uint8_t)mcs_to_int(vm, mcs_index(argv[0], off + done + (uint32_t)i));
        size_t sent = 0;
        while (sent < n) {
            int r = mcs_vfs_write(&s->file, buf + sent, n - sent);
            if (r <= 0 || (size_t)r > n - sent) return io_fail(vm, r < 0 ? r : MCS_VFS_EIO, s->path, false);
            sent += (size_t)r; s->position += r;
            if (!s->append && s->position > s->length) s->length = s->position;
        }
        done += (uint32_t)n;
    }
    if (count) (void)stream_written(vm, s);
    return mcs_null();
}
NATIVE(fs_read_byte) {
    STREAM(); if (!stream_access(vm, s, 1)) return mcs_null();
    uint8_t b; int r = mcs_vfs_read(&s->file, &b, 1);
    if (r < 0) return io_fail(vm, r, s->path, false);
    if (r > 1 || (r && s->position == 0x7fffffff)) return io_fail(vm, MCS_VFS_EINVAL, s->path, false);
    if (r) s->position++;
    return mcs_int(r ? b : -1);
}
NATIVE(fs_write_byte) {
    STREAM(); if (!stream_access(vm, s, 2)) return mcs_null();
    mcs_int_t v = mcs_to_int(vm, argv[0]); if (mcs_has_exception(vm)) return mcs_null();
    if (v < 0 || v > 255) { mcs_raise(vm, "ArgumentOutOfRangeException", "Byte value outside 0..255"); return mcs_null(); }
    if (s->position == 0x7fffffff) return io_fail(vm, MCS_VFS_ENOSPC, s->path, false);
    uint8_t b = (uint8_t)v; int r = mcs_vfs_write(&s->file, &b, 1);
    if (r != 1) return io_fail(vm, r < 0 ? r : MCS_VFS_EIO, s->path, false);
    s->position++; (void)stream_written(vm, s);
    return mcs_null();
}
NATIVE(fs_seek) {
    STREAM(); mcs_int_t off = mcs_to_int(vm, argv[0]), origin = mcs_to_int(vm, argv[1]);
    if (mcs_has_exception(vm)) return mcs_null();
    if (origin < 0 || origin > 2 || off < -2147483647 || (uint64_t)(off < 0 ? -off : off) > 0x7fffffff) { mcs_raise(vm, "ArgumentOutOfRangeException", "Invalid seek offset/origin"); return mcs_null(); }
    if (!s->file.ops->seek) { mcs_raise(vm, "NotSupportedException", "Backend does not support seeking"); return mcs_null(); }
    int32_t p, old = s->position;
    int e = mcs_vfs_seek(&s->file, (int32_t)off, (int)origin, &p);
    if (e) return io_fail(vm, e, s->path, false);
    if (s->append && p < s->append_start) { (void)mcs_vfs_seek(&s->file, old, 0, &p); mcs_raise(vm, "IOException", "Cannot seek before the append origin"); return mcs_null(); }
    s->position = p; s->last_op = 0; return mcs_int(p);
}
NATIVE(fs_position) { STREAM(); return mcs_int(s->position); }
NATIVE(fs_set_position) {
    mcs_value_t args[2] = { argv[0], mcs_int(0) };
    return fs_seek(vm, self, 2, args);
}
NATIVE(fs_length) {
    STREAM();
    if (s->file.ops->seek) {
        int32_t length, p;
        int e = mcs_vfs_seek(&s->file, 0, 2, &length);
        if (e) return io_fail(vm, e, s->path, false);
        e = mcs_vfs_seek(&s->file, s->position, 0, &p);
        if (e) return io_fail(vm, e, s->path, false);
        s->length = length;
    }
    return mcs_int(s->length);
}
NATIVE(fs_can_read) { file_stream_t* s = mcs_check_userdata(vm, self, &stream_def); return mcs_bool(s && s->opened && (s->access & 1)); }
NATIVE(fs_can_write) { file_stream_t* s = mcs_check_userdata(vm, self, &stream_def); return mcs_bool(s && s->opened && (s->access & 2)); }
NATIVE(fs_can_seek) { file_stream_t* s = mcs_check_userdata(vm, self, &stream_def); return mcs_bool(s && s->opened && s->file.ops->seek); }
NATIVE(fs_flush) { STREAM(); int e = mcs_vfs_flush(&s->file); return e ? io_fail(vm, e, s->path, false) : mcs_null(); }
NATIVE(fs_close) {
    file_stream_t* s = mcs_check_userdata(vm, self, &stream_def);
    if (s && s->opened) { s->opened = 0; int e = mcs_vfs_close(&s->file); if (e) return io_fail(vm, e, s->path, false); }
    return mcs_null();
}
static const mcs_reg_t stream_members[] = {
    MCS_FN("Read", fs_read, 3), MCS_FN("Write", fs_write, 3),
    MCS_FN("ReadByte", fs_read_byte, 0), MCS_FN("WriteByte", fs_write_byte, 1),
    MCS_FN("Seek", fs_seek, 2), MCS_FN("Flush", fs_flush, 0),
    MCS_FN("Close", fs_close, 0), MCS_FN("Dispose", fs_close, 0),
    MCS_GET("Position", fs_position), MCS_SET("Position", fs_set_position), MCS_GET("Length", fs_length),
    MCS_GET("CanRead", fs_can_read), MCS_GET("CanWrite", fs_can_write), MCS_GET("CanSeek", fs_can_seek), MCS_REG_END
};
static const mcs_class_def_t stream_def = { "FileStream", sizeof(file_stream_t), stream_ctor, stream_free, stream_members, NULL };
static const mcs_const_t file_modes[] = { {"CreateNew", 1}, {"Create", 2}, {"Open", 3}, {"OpenOrCreate", 4}, {"Truncate", 5}, {"Append", 6}, {NULL, 0} };
static const mcs_const_t file_access[] = { {"Read", 1}, {"Write", 2}, {"ReadWrite", 3}, {NULL, 0} };
static const mcs_const_t seek_origins[] = { {"Begin", 0}, {"Current", 1}, {"End", 2}, {NULL, 0} };
static mcs_value_t stream_new(mcs_vm_t* vm, mcs_value_t path, int mode, int access) {
    mcs_value_t args[3] = { path, mcs_int(mode), mcs_int(access) }, out = mcs_null();
    (void)mcs_new_object(vm, "FileStream", 3, args, &out); return out;
}
NATIVE(f_open_read) { return stream_new(vm, argv[0], 3, 1); }
NATIVE(f_open_write) { return stream_new(vm, argv[0], 4, 2); }
NATIVE(f_create) { return stream_new(vm, argv[0], 2, 3); }
NATIVE(f_open) {
    if (argc < 2 || argc > 3) { mcs_raise(vm, "ArgumentException", "File.Open(path, mode[, access]) expected"); return mcs_null(); }
    mcs_value_t out = mcs_null(); (void)mcs_new_object(vm, "FileStream", argc, argv, &out); return out;
}


#endif /* MCS_ENABLE_FILE_STREAMS */
static const mcs_reg_t file_fns[] = {
#if MCS_ENABLE_FILE_STREAMS
    MCS_FN("OpenRead", f_open_read, 1), MCS_FN("OpenWrite", f_open_write, 1),
    MCS_FN("Open", f_open, -1), MCS_FN("Create", f_create, 1),
#endif
    MCS_FN("ReadAllText", f_read_text, 1),
    MCS_FN("WriteAllText", f_write_text, 2),
    MCS_FN("AppendAllText", f_append_text, 2),
    MCS_FN("ReadAllLines", f_read_lines, 1),
    MCS_FN("WriteAllLines", f_write_lines, 2),
    MCS_FN("AppendAllLines", f_append_lines, 2),
    MCS_FN("ReadAllBytes", f_read_bytes, 1),
    MCS_FN("WriteAllBytes", f_write_bytes, 2),
    MCS_FN("Exists", f_exists, 1),
    MCS_FN("Delete", f_delete, 1),
    MCS_FN("Copy", f_copy, -1),
    MCS_FN("Move", f_move, 2),
    MCS_FN("GetLength", f_length, 1),   /* stands in for new FileInfo(p).Length */
    MCS_REG_END
};

/* -------------------------------------------------------------- Directory */
NATIVE(d_exists) { const char* p = mcs_cstr(argv[0]); return mcs_bool(p && is_dir(VFS(), p)); }

NATIVE(d_create) {
    ARG_PATH(0, p);
    char norm[MCS_VFS_PATH_MAX];
    int e = mcs_vfs_normalize(p, norm, sizeof norm);
    if (e) return io_fail(vm, e, p, true);
    /* create every missing component, like .NET */
    for (char* s = norm + 1;; s++) {
        if (*s == '/' || *s == 0) {
            char c = *s; *s = 0;
            if (!is_dir(VFS(), norm)) {
                e = mcs_vfs_mkdir(VFS(), norm);
                if (e && e != MCS_VFS_EEXIST) return io_fail(vm, e, norm, true);
            }
            *s = c;
            if (!c) break;
        }
    }
    return mcs_string(vm, norm);
}

typedef struct { mcs_vm_t* vm; mcs_value_t list; const char* dir; int want; int panic_code; /* 1 files 2 dirs 3 both */ } collect_t;
typedef struct { collect_t* collect; const char* path; } collect_item_t;
static void collect_add(void* ud) {
    collect_item_t* item = (collect_item_t*)ud;
    collect_t* c = item->collect;
    mcs_list_add(c->vm, c->list, mcs_string(c->vm, item->path));
}
static int collect(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    collect_t* c = (collect_t*)ud;
    if (!((st->is_dir ? 2 : 1) & c->want)) return 0;
    char full[MCS_VFS_PATH_MAX * 2];
    snprintf(full, sizeof full, "%s%s%s", c->dir, strcmp(c->dir, "/") ? "/" : "", name);
    collect_item_t item = { c, full };
    c->panic_code = mcs_protect_native(c->vm, collect_add, &item);
    return c->panic_code != 0;
}

static mcs_value_t list_entries(mcs_vm_t* vm, mcs_value_t* argv, int want) {
    ARG_PATH(0, p);
    char norm[MCS_VFS_PATH_MAX];
    int e = mcs_vfs_normalize(p, norm, sizeof norm);
    if (e) return io_fail(vm, e, p, true);
    if (!is_dir(VFS(), norm)) return io_fail(vm, MCS_VFS_ENOENT, p, true);
    mcs_value_t list = mcs_new_list(vm);
    mcs_push_root(vm, list);
    collect_t c = { vm, list, norm, want, 0 };
    e = mcs_vfs_list(VFS(), norm, collect, &c);
    mcs_rethrow_native(vm, c.panic_code);
    uint32_t n = mcs_len(list);
    mcs_value_t arr = mcs_new_array(vm, n);
    for (uint32_t i = 0; i < n; i++) mcs_set_index(arr, i, mcs_index(list, i));
    mcs_pop_root(vm, 1);
    return e ? io_fail(vm, e, p, true) : arr;
}
NATIVE(d_files) { return list_entries(vm, argv, 1); }
NATIVE(d_dirs) { return list_entries(vm, argv, 2); }
NATIVE(d_entries) { return list_entries(vm, argv, 3); }

static int remove_tree(mcs_vfs_t* vfs, const char* path);
typedef struct { mcs_vfs_t* vfs; const char* dir; int err; } rm_t;
static int rm_child(void* ud, const char* name, const mcs_vfs_stat_t* st) {
    rm_t* r = (rm_t*)ud;
    char full[MCS_VFS_PATH_MAX];
    snprintf(full, sizeof full, "%s/%s", strcmp(r->dir, "/") ? r->dir : "", name);
    r->err = st->is_dir ? remove_tree(r->vfs, full) : mcs_vfs_remove(r->vfs, full);
    return r->err;
}
/* Both built-in backends snapshot a directory before calling back, so
 * children can be deleted from inside the listing callback. */
static int remove_tree(mcs_vfs_t* vfs, const char* path) {
    rm_t r = { vfs, path, 0 };
    int e = mcs_vfs_list(vfs, path, rm_child, &r);
    if (e) return e;
    if (r.err) return r.err;
    return mcs_vfs_remove(vfs, path);
}
NATIVE(d_delete) {
    ARG_PATH(0, p);
    if (!is_dir(VFS(), p)) return io_fail(vm, MCS_VFS_ENOENT, p, true);
    bool recursive = argc > 1 && mcs_truthy(argv[1]);
    int e = recursive ? remove_tree(VFS(), p) : mcs_vfs_remove(VFS(), p);
    if (e == MCS_VFS_ENOTEMPTY) { mcs_raise(vm, "IOException", "Directory not empty : '%s'", p); return mcs_null(); }
    return e ? io_fail(vm, e, p, true) : mcs_null();
}
NATIVE(d_cwd) { return mcs_string(vm, "/"); }

static const mcs_reg_t dir_fns[] = {
    MCS_FN("Exists", d_exists, 1),
    MCS_FN("CreateDirectory", d_create, 1),
    MCS_FN("GetFiles", d_files, 1),
    MCS_FN("GetDirectories", d_dirs, 1),
    MCS_FN("GetFileSystemEntries", d_entries, 1),
    MCS_FN("Delete", d_delete, -1),
    MCS_FN("GetCurrentDirectory", d_cwd, 0),
    MCS_REG_END
};

/* -------------------------------------------------------------- Path */
static const char* base_name(const char* p) {
    const char* s = strrchr(p, '/');
    const char* b = strrchr(p, '\\');
    if (b > s) s = b;
    return s ? s + 1 : p;
}
NATIVE(p_combine) {
    char out[MCS_VFS_PATH_MAX * 2];
    size_t n = 0;
    for (int i = 0; i < argc; i++) {
        const char* s = mcs_to_cstr(vm, argv[i]);
        if (mcs_has_exception(vm)) return mcs_null();
        if (!*s) continue;
        if (s[0] == '/' || s[0] == '\\') n = 0;   /* rooted component restarts, as in .NET */
        else if (n && out[n - 1] != '/' && out[n - 1] != '\\' && n + 1 < sizeof out) out[n++] = '/';
        size_t l = strlen(s);
        if (n + l >= sizeof out) { mcs_raise(vm, "ArgumentException", "path too long"); return mcs_null(); }
        memcpy(out + n, s, l); n += l;
    }
    return mcs_string_n(vm, out, n);
}
NATIVE(p_filename) { ARG_PATH(0, p); return mcs_string(vm, base_name(p)); }
NATIVE(p_ext) {
    ARG_PATH(0, p);
    const char* b = base_name(p);
    const char* dot = strrchr(b, '.');
    return mcs_string(vm, dot && dot[1] ? dot : "");
}
NATIVE(p_stem) {
    ARG_PATH(0, p);
    const char* b = base_name(p);
    const char* dot = strrchr(b, '.');
    return mcs_string_n(vm, b, dot ? (size_t)(dot - b) : strlen(b));
}
NATIVE(p_dirname) {
    ARG_PATH(0, p);
    const char* b = base_name(p);
    if (b == p) return mcs_string(vm, "");
    size_t n = (size_t)(b - p - 1);
    if (n == 0) return mcs_string(vm, "/");
    return mcs_string_n(vm, p, n);
}
NATIVE(p_full) {
    ARG_PATH(0, p);
    char norm[MCS_VFS_PATH_MAX];
    if (mcs_vfs_normalize(p, norm, sizeof norm)) { mcs_raise(vm, "ArgumentException", "path too long"); return mcs_null(); }
    return mcs_string(vm, norm);
}
static const mcs_reg_t path_fns[] = {
    MCS_FN("Combine", p_combine, -1),
    MCS_FN("GetFileName", p_filename, 1),
    MCS_FN("GetExtension", p_ext, 1),
    MCS_FN("GetFileNameWithoutExtension", p_stem, 1),
    MCS_FN("GetDirectoryName", p_dirname, 1),
    MCS_FN("GetFullPath", p_full, 1),
    MCS_REG_END
};

/* -------------------------------------------------------------- DriveInfo */
/* new DriveInfo("/") describes the filesystem mounted at (or holding) a path;
 * DriveInfo.GetDrives() lists every mount. Sizes are re-read on each access. */
typedef struct { char path[MCS_VFS_PATH_MAX]; } drive_t;
static const mcs_class_def_t drive_def;

static void drive_ctor(mcs_vm_t* vm, mcs_value_t self, int argc, mcs_value_t* argv) {
    drive_t* d = (drive_t*)mcs_check_userdata(vm, self, &drive_def);
    if (mcs_has_exception(vm)) return;
    if (argc != 1 || !mcs_cstr(argv[0])) { mcs_raise(vm, "ArgumentNullException", "Value cannot be null. (Parameter 'driveName')"); return; }
    const char* p = mcs_cstr(argv[0]);
    if (!*p || mcs_vfs_normalize(p, d->path, sizeof d->path))
        mcs_raise(vm, "ArgumentException", "Drive name must be a root directory (i.e. 'C:\\'), a drive letter ('C'), or a valid path. (Parameter 'driveName')");
}

static bool drive_stat(mcs_vm_t* vm, mcs_value_t self, mcs_vfs_statfs_t* st, bool raise) {
    drive_t* d = (drive_t*)mcs_check_userdata(vm, self, &drive_def);
    if (mcs_has_exception(vm)) return false;
    int e = VFS() ? mcs_vfs_statfs(VFS(), d->path, st) : MCS_VFS_ENOENT;
    if (!e) return true;
    if (!raise) return false;
    if (e == MCS_VFS_ENOENT) mcs_raise(vm, "DriveNotFoundException", "Could not find the drive '%s'. The drive might not be ready or might not be mapped.", d->path);
    else if (e == MCS_VFS_EINVAL) mcs_raise(vm, "IOException", "The filesystem at '%s' cannot report its size.", d->path);
    else mcs_raise(vm, "IOException", "%s: '%s'", mcs_vfs_strerror(e), d->path);
    return false;
}
static mcs_value_t bytes_val(uint64_t n) {
    mcs_int_t max = (mcs_int_t)(((uint64_t)1 << (sizeof(mcs_int_t) * 8 - 1)) - 1);
    return mcs_int(n > (uint64_t)max ? max : (mcs_int_t)n);
}
NATIVE(dr_total) { mcs_vfs_statfs_t st; return drive_stat(vm, self, &st, true) ? bytes_val(st.total) : mcs_null(); }
NATIVE(dr_free) { mcs_vfs_statfs_t st; return drive_stat(vm, self, &st, true) ? bytes_val(st.free) : mcs_null(); }
NATIVE(dr_format) { mcs_vfs_statfs_t st; return drive_stat(vm, self, &st, true) ? mcs_string(vm, st.format ? st.format : "") : mcs_null(); }
NATIVE(dr_ready) { mcs_vfs_statfs_t st; return mcs_bool(drive_stat(vm, self, &st, false)); }
NATIVE(dr_name) {
    drive_t* d = (drive_t*)mcs_check_userdata(vm, self, &drive_def);
    return mcs_has_exception(vm) ? mcs_null() : mcs_string(vm, d->path);
}
NATIVE(dr_drives) {
    mcs_vfs_t* vfs = VFS();
    int n = vfs ? vfs->count : 0;
    mcs_value_t arr = mcs_new_array(vm, (uint32_t)n);
    mcs_push_root(vm, arr);
    for (int i = 0; i < n; i++) {
        mcs_value_t name = mcs_string(vm, vfs->mounts[i].prefix), d;
        mcs_push_root(vm, name);
        mcs_result_t r = mcs_new_object(vm, "DriveInfo", 1, &name, &d);
        mcs_pop_root(vm, 1);
        if (r != MCS_OK || mcs_has_exception(vm)) break;
        mcs_set_index(arr, (uint32_t)i, d);
    }
    mcs_pop_root(vm, 1);
    return arr;
}
static const mcs_reg_t drive_members[] = {
    MCS_GET("Name", dr_name), MCS_GET("TotalSize", dr_total),
    MCS_GET("TotalFreeSpace", dr_free), MCS_GET("AvailableFreeSpace", dr_free),
    MCS_GET("DriveFormat", dr_format), MCS_GET("IsReady", dr_ready),
    MCS_FN("ToString", dr_name, 0), MCS_REG_END
};
static const mcs_reg_t drive_statics[] = { MCS_FN("GetDrives", dr_drives, 0), MCS_REG_END };
static const mcs_class_def_t drive_def = { "DriveInfo", sizeof(drive_t), drive_ctor, NULL, drive_members, drive_statics };

void mcs_fs_open_lib(mcs_vm_t* vm, mcs_vfs_t* vfs) {
    mcs_set_ext(vm, MCS_EXT_VFS, vfs);

#if MCS_ENABLE_FILE_STREAMS
    mcs_register_class(vm, &stream_def);
    mcs_register_consts(vm, "FileMode", file_modes);
    mcs_register_consts(vm, "FileAccess", file_access);
    mcs_register_consts(vm, "SeekOrigin", seek_origins);
#endif
    mcs_register_module(vm, "File", file_fns);
    mcs_register_module(vm, "Directory", dir_fns);
    mcs_register_module(vm, "Path", path_fns);
    mcs_register_class(vm, &drive_def);
}
#endif
