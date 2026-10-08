/* MicroCS - byte helpers of the standard library (always available, no HAL needed):
 *   Encoding.UTF8 / Encoding.ASCII   GetBytes, GetString, GetByteCount
 *   BitConverter                     To{Int16,UInt16,Int32,UInt32,Int64,Boolean,Single,Double}, GetBytes, ToString
 *   BinaryPrimitives                 Read/Write{Int16,UInt16,Int32,UInt32,Int64}{Big,Little}Endian
 *   Convert (added to the class)     ToBase64String, FromBase64String, ToHexString, ToHexStringLower, FromHexString
 * Byte arrays are ordinary MicroCS arrays of ints 0..255 (List<byte> is accepted too). */
#include "mcs_lib.h"
#include <string.h>

#if MCS_ENABLE_BYTES

#define OPT_INT(i, var, def) int var = argc > (i) ? (int)mcs_to_int(vm, argv[i]) : (def); CHECK()

static mcs_value_t range_fail(mcs_vm_t* vm, const char* what) {
    mcs_throw(vm, EXC_ARGRANGE, "Specified argument was out of the range of valid values. (Parameter '%s')", what);
    return mcs_null();
}
static bool seq_arg(mcs_vm_t* vm, mcs_value_t v) {
    if (mcs_is_null(v)) { mcs_throw(vm, EXC_ARGNULL, "Value cannot be null."); return false; }
    if (!lib_is_seq(v)) { mcs_throw(vm, EXC_ARGUMENT, "expected byte[], got %s", mcs_type_name(vm, v)); return false; }
    return true;
}
static mcs_value_t bytes_val(mcs_vm_t* vm, const uint8_t* b, size_t n) {
    mcs_value_t a = mcs_new_array(vm, (uint32_t)n);
    for (size_t i = 0; i < n; i++) mcs_set_index(a, (uint32_t)i, mcs_int(b[i]));
    return a;
}
static int byte_at(mcs_vm_t* vm, mcs_value_t a, uint32_t i) { return (int)mcs_to_int(vm, mcs_index(a, i)) & 0xFF; }
/* (bytes[, offset, length]) -> validated span */
static bool span_args(mcs_vm_t* vm, int argc, mcs_value_t* argv, uint32_t* start, uint32_t* count) {
    if (!seq_arg(vm, argv[0])) return false;
    uint32_t len = mcs_len(argv[0]);
    mcs_int_t s = argc > 1 ? mcs_to_int(vm, argv[1]) : 0;
    if (vm->has_exc) return false;
    mcs_int_t c = argc > 2 ? mcs_to_int(vm, argv[2]) : (mcs_int_t)len - s;
    if (vm->has_exc) return false;
    if (s < 0 || c < 0 || (mcs_uint_t)s + (mcs_uint_t)c > len) { range_fail(vm, s < 0 || (mcs_uint_t)s > len ? "offset" : "length"); return false; }
    *start = (uint32_t)s; *count = (uint32_t)c;
    return true;
}
static char* tmp_alloc(mcs_vm_t* vm, size_t n) { return (char*)mcs_realloc(vm, NULL, 0, n ? n : 1); }
static void tmp_free(mcs_vm_t* vm, char* p, size_t n) { mcs_realloc(vm, p, n ? n : 1, 0); }

/* ------------------------------------------------------------ Encoding */
typedef struct { int ascii; } enc_t;
static const mcs_class_def_t enc_def;
NATIVE(enc_getbytes) {
    enc_t* e = (enc_t*)mcs_check_userdata(vm, self, &enc_def); if (!e) return mcs_null();
    if (!mcs_is_string(argv[0])) { mcs_throw(vm, EXC_ARGUMENT, "expected a string"); return mcs_null(); }
    const uint8_t* p = (const uint8_t*)mcs_cstr(argv[0]);
    size_t n = mcs_strlen(argv[0]), out = 0;
    for (size_t i = 0; i < n; i++) if (!e->ascii || p[i] < 0x80 || p[i] >= 0xC0) out++;
    mcs_value_t a = mcs_new_array(vm, (uint32_t)out);
    size_t k = 0;
    for (size_t i = 0; i < n; i++) {
        if (!e->ascii || p[i] < 0x80) mcs_set_index(a, (uint32_t)k++, mcs_int(p[i]));
        else if (p[i] >= 0xC0) mcs_set_index(a, (uint32_t)k++, mcs_int('?'));   /* one '?' per code point */
    }
    return a;
}
NATIVE(enc_getstring) {     /* (bytes[, index, count]) */
    enc_t* e = (enc_t*)mcs_check_userdata(vm, self, &enc_def); if (!e) return mcs_null();
    uint32_t start, count;
    if (!span_args(vm, argc, argv, &start, &count)) return mcs_null();
    char tmp[128]; char* buf = count <= sizeof tmp ? tmp : tmp_alloc(vm, count);
    if (!buf) return mcs_null();
    for (uint32_t i = 0; i < count; i++) {
        int b = byte_at(vm, argv[0], start + i);
        buf[i] = (char)(e->ascii && b >= 0x80 ? '?' : b);
    }
    mcs_value_t s = mcs_string_n(vm, buf, count);
    if (buf != tmp) tmp_free(vm, buf, count);
    return s;
}
NATIVE(enc_getbytecount) {
    enc_t* e = (enc_t*)mcs_check_userdata(vm, self, &enc_def); if (!e) return mcs_null();
    if (!mcs_is_string(argv[0])) { mcs_throw(vm, EXC_ARGUMENT, "expected a string"); return mcs_null(); }
    const uint8_t* p = (const uint8_t*)mcs_cstr(argv[0]);
    size_t n = mcs_strlen(argv[0]), out = 0;
    for (size_t i = 0; i < n; i++) if (!e->ascii || p[i] < 0x80 || p[i] >= 0xC0) out++;
    return mcs_int((mcs_int_t)out);
}
static const mcs_reg_t enc_members[] = {
    MCS_FN("GetBytes", enc_getbytes, 1), MCS_FN("GetString", enc_getstring, -1),
    MCS_FN("GetByteCount", enc_getbytecount, 1), MCS_REG_END
};
static const mcs_class_def_t enc_def = { "__Encoding", sizeof(enc_t), NULL, NULL, enc_members, NULL };
/* Encoding.UTF8 / Encoding.ASCII: created on first use, then stored as static
 * fields (found before the getter from then on, so the object is shared) */
static mcs_value_t enc_get(mcs_vm_t* vm, int ascii) {
    mcs_value_t obj;
    if (mcs_new_object(vm, "__Encoding", 0, NULL, &obj) != MCS_OK) return mcs_null();
    ((enc_t*)mcs_userdata(obj))->ascii = ascii;
    mcs_push_root(vm, obj);
    mcs_module_set(vm, "Encoding", ascii ? "ASCII" : "UTF8", obj);
    mcs_pop_root(vm, 1);
    return obj;
}
NATIVE(enc_utf8) { return enc_get(vm, 0); }
NATIVE(enc_ascii) { return enc_get(vm, 1); }
static const mcs_reg_t enc_fns[] = { MCS_GET("UTF8", enc_utf8), MCS_GET("ASCII", enc_ascii), MCS_REG_END };

/* ------------------------------------------------------------ BitConverter (little-endian) */
static bool bc_span(mcs_vm_t* vm, int argc, mcs_value_t* argv, int width, uint8_t* out) {
    if (!seq_arg(vm, argv[0])) return false;
    mcs_int_t start = argc > 1 ? mcs_to_int(vm, argv[1]) : 0;
    if (vm->has_exc) return false;
    if (start < 0 || (mcs_uint_t)start + (mcs_uint_t)width > mcs_len(argv[0])) { range_fail(vm, "startIndex"); return false; }
    for (int i = 0; i < width; i++) out[i] = (uint8_t)byte_at(vm, argv[0], (uint32_t)start + (uint32_t)i);
    return !vm->has_exc;
}
static uint64_t le(const uint8_t* b, int w) { uint64_t v = 0; for (int i = w - 1; i >= 0; i--) v = (v << 8) | b[i]; return v; }
static uint64_t be(const uint8_t* b, int w) { uint64_t v = 0; for (int i = 0; i < w; i++) v = (v << 8) | b[i]; return v; }
NATIVE(bc_int16)  { uint8_t b[2]; if (!bc_span(vm, argc, argv, 2, b)) return mcs_null(); return mcs_int((int16_t)le(b, 2)); }
NATIVE(bc_uint16) { uint8_t b[2]; if (!bc_span(vm, argc, argv, 2, b)) return mcs_null(); return mcs_int((uint16_t)le(b, 2)); }
NATIVE(bc_int32)  { uint8_t b[4]; if (!bc_span(vm, argc, argv, 4, b)) return mcs_null(); return mcs_int((int32_t)le(b, 4)); }
NATIVE(bc_uint32) { uint8_t b[4]; if (!bc_span(vm, argc, argv, 4, b)) return mcs_null(); return mcs_int((mcs_int_t)(uint32_t)le(b, 4)); }
NATIVE(bc_int64)  { uint8_t b[8]; if (!bc_span(vm, argc, argv, 8, b)) return mcs_null(); return mcs_int((mcs_int_t)(int64_t)le(b, 8)); }
NATIVE(bc_bool)   { uint8_t b[1]; if (!bc_span(vm, argc, argv, 1, b)) return mcs_null(); return mcs_bool(b[0] != 0); }
#if MCS_ENABLE_FLOAT
NATIVE(bc_single) { uint8_t b[4]; if (!bc_span(vm, argc, argv, 4, b)) return mcs_null(); uint32_t u = (uint32_t)le(b, 4); float f; memcpy(&f, &u, 4); return mcs_float((mcs_float_t)f); }
NATIVE(bc_double) {
    uint8_t b[8]; if (!bc_span(vm, argc, argv, 8, b)) return mcs_null();
    uint64_t u = le(b, 8); double d; memcpy(&d, &u, 8); return mcs_float((mcs_float_t)d);
}
#endif
static int wide64(int64_t v) { return v < INT32_MIN || v > (int64_t)UINT32_MAX; }
NATIVE(bc_getbytes) {   /* bool -> 1, int (32-bit range) -> 4, larger int -> 8, double -> 8 */
    uint8_t b[8]; int w; uint64_t u;
    if (mcs_is_bool(argv[0])) { b[0] = (uint8_t)(mcs_truthy(argv[0]) ? 1 : 0); return bytes_val(vm, b, 1); }
#if MCS_ENABLE_FLOAT
    if (mcs_is_float(argv[0])) { double d = (double)mcs_to_float(vm, argv[0]); memcpy(&u, &d, 8); w = 8; }
    else
#endif
    {
        mcs_int_t i = mcs_to_int(vm, argv[0]);
        CHECK();
        w = wide64((int64_t)i) ? 8 : 4;
        u = (uint64_t)(int64_t)i;
    }
    for (int k = 0; k < w; k++) b[k] = (uint8_t)(u >> (8 * k));
    return bytes_val(vm, b, (size_t)w);
}
static const char hx_up[] = "0123456789ABCDEF";
static const char hx_lo[] = "0123456789abcdef";
/* hex dump of bytes[start, start+count); sep = 0 for none */
static mcs_value_t hex_string(mcs_vm_t* vm, mcs_value_t a, uint32_t start, uint32_t count, char sep, const char* hx) {
    size_t step = sep ? 3 : 2, n = count ? (size_t)count * step - (sep ? 1 : 0) : 0;
    char* buf = tmp_alloc(vm, n);
    if (!buf) return mcs_null();
    for (uint32_t i = 0; i < count; i++) {
        int v = byte_at(vm, a, start + i);
        buf[i * step] = hx[v >> 4]; buf[i * step + 1] = hx[v & 15];
        if (sep && i + 1 < count) buf[i * step + 2] = sep;
    }
    mcs_value_t s = mcs_string_n(vm, buf, n);
    tmp_free(vm, buf, n);
    return s;
}
NATIVE(bc_tostring) {   /* "01-AB-FF" like .NET; (bytes[, start[, length]]) */
    uint32_t start, count;
    if (!span_args(vm, argc, argv, &start, &count)) return mcs_null();
    return hex_string(vm, argv[0], start, count, '-', hx_up);
}
NATIVE(bc_little) { return mcs_bool(true); }
static const mcs_reg_t bitconv_fns[] = {
    MCS_FN("ToInt16", bc_int16, -1), MCS_FN("ToUInt16", bc_uint16, -1), MCS_FN("ToInt32", bc_int32, -1),
    MCS_FN("ToUInt32", bc_uint32, -1), MCS_FN("ToInt64", bc_int64, -1), MCS_FN("ToBoolean", bc_bool, -1),
#if MCS_ENABLE_FLOAT
    MCS_FN("ToSingle", bc_single, -1), MCS_FN("ToDouble", bc_double, -1),
#endif
    MCS_FN("GetBytes", bc_getbytes, 1), MCS_FN("ToString", bc_tostring, -1),
    MCS_GET("IsLittleEndian", bc_little), MCS_REG_END
};

/* ------------------------------------------------------------ BinaryPrimitives
 * ReadXxx(bytes[, offset]) / WriteXxx(bytes, value[, offset]); the optional
 * offset is a MicroCS extension (C# would slice a Span). */
#define BP_READ(name, w, conv, order) NATIVE(name) { uint8_t b[8]; if (!bc_span(vm, argc, argv, w, b)) return mcs_null(); return mcs_int(conv order(b, w)); }
BP_READ(bp_r16be, 2, (int16_t), be)  BP_READ(bp_r16le, 2, (int16_t), le)
BP_READ(bp_ru16be, 2, (uint16_t), be) BP_READ(bp_ru16le, 2, (uint16_t), le)
BP_READ(bp_r32be, 4, (int32_t), be)  BP_READ(bp_r32le, 4, (int32_t), le)
BP_READ(bp_ru32be, 4, (mcs_int_t)(uint32_t), be) BP_READ(bp_ru32le, 4, (mcs_int_t)(uint32_t), le)
BP_READ(bp_r64be, 8, (mcs_int_t)(int64_t), be) BP_READ(bp_r64le, 8, (mcs_int_t)(int64_t), le)
static mcs_value_t bp_write(mcs_vm_t* vm, int argc, mcs_value_t* argv, int w, bool big) {
    if (argc < 2) { mcs_throw(vm, EXC_ARGUMENT, "expected (destination, value)"); return mcs_null(); }
    if (!seq_arg(vm, argv[0])) return mcs_null();
    uint64_t v = (uint64_t)(int64_t)mcs_to_int(vm, argv[1]); CHECK();
    mcs_int_t start = argc > 2 ? mcs_to_int(vm, argv[2]) : 0; CHECK();
    if (start < 0 || (mcs_uint_t)start + (mcs_uint_t)w > mcs_len(argv[0])) return range_fail(vm, "destination");
    for (int i = 0; i < w; i++) {
        int sh = big ? 8 * (w - 1 - i) : 8 * i;
        mcs_set_index(argv[0], (uint32_t)start + (uint32_t)i, mcs_int((mcs_int_t)((v >> sh) & 0xFF)));
    }
    return mcs_null();
}
NATIVE(bp_w16be) { return bp_write(vm, argc, argv, 2, true); }  NATIVE(bp_w16le) { return bp_write(vm, argc, argv, 2, false); }
NATIVE(bp_w32be) { return bp_write(vm, argc, argv, 4, true); }  NATIVE(bp_w32le) { return bp_write(vm, argc, argv, 4, false); }
NATIVE(bp_w64be) { return bp_write(vm, argc, argv, 8, true); }  NATIVE(bp_w64le) { return bp_write(vm, argc, argv, 8, false); }
static const mcs_reg_t binprim_fns[] = {
    MCS_FN("ReadInt16BigEndian", bp_r16be, -1), MCS_FN("ReadInt16LittleEndian", bp_r16le, -1),
    MCS_FN("ReadUInt16BigEndian", bp_ru16be, -1), MCS_FN("ReadUInt16LittleEndian", bp_ru16le, -1),
    MCS_FN("ReadInt32BigEndian", bp_r32be, -1), MCS_FN("ReadInt32LittleEndian", bp_r32le, -1),
    MCS_FN("ReadUInt32BigEndian", bp_ru32be, -1), MCS_FN("ReadUInt32LittleEndian", bp_ru32le, -1),
    MCS_FN("ReadInt64BigEndian", bp_r64be, -1), MCS_FN("ReadInt64LittleEndian", bp_r64le, -1),
    MCS_FN("WriteInt16BigEndian", bp_w16be, -1), MCS_FN("WriteInt16LittleEndian", bp_w16le, -1),
    MCS_FN("WriteUInt16BigEndian", bp_w16be, -1), MCS_FN("WriteUInt16LittleEndian", bp_w16le, -1),
    MCS_FN("WriteInt32BigEndian", bp_w32be, -1), MCS_FN("WriteInt32LittleEndian", bp_w32le, -1),
    MCS_FN("WriteUInt32BigEndian", bp_w32be, -1), MCS_FN("WriteUInt32LittleEndian", bp_w32le, -1),
    MCS_FN("WriteInt64BigEndian", bp_w64be, -1), MCS_FN("WriteInt64LittleEndian", bp_w64le, -1),
    MCS_REG_END
};

/* ------------------------------------------------------------ Convert: Base64 / hex */
static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
NATIVE(cv_tobase64) {   /* (bytes[, offset, length]) */
    uint32_t start, count;
    if (!span_args(vm, argc, argv, &start, &count)) return mcs_null();
    size_t n = ((size_t)count + 2) / 3 * 4;
    char* out = tmp_alloc(vm, n);
    if (!out) return mcs_null();
    size_t o = 0;
    for (uint32_t i = 0; i < count; i += 3) {
        uint32_t rem = count - i, v = (uint32_t)byte_at(vm, argv[0], start + i) << 16;
        if (rem > 1) v |= (uint32_t)byte_at(vm, argv[0], start + i + 1) << 8;
        if (rem > 2) v |= (uint32_t)byte_at(vm, argv[0], start + i + 2);
        out[o++] = b64[(v >> 18) & 63]; out[o++] = b64[(v >> 12) & 63];
        out[o++] = rem > 1 ? b64[(v >> 6) & 63] : '=';
        out[o++] = rem > 2 ? b64[v & 63] : '=';
    }
    mcs_value_t s = mcs_string_n(vm, out, n);
    tmp_free(vm, out, n);
    return s;
}
static int b64_val(int c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}
static bool b64_space(int c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
NATIVE(cv_frombase64) {   /* whitespace ignored, like .NET */
    mcs_string_t* str = lib_need_str(vm, argv[0], "s"); CHECK();
    const uint8_t* p = (const uint8_t*)str->chars;
    size_t len = str->len, sig = 0, pad = 0;
    bool bad = false;
    for (size_t i = 0; i < len && !bad; i++) {
        int c = p[i];
        if (b64_space(c)) continue;
        if (c == '=') pad++;
        else if (pad || b64_val(c) < 0) bad = true;   /* data after padding / illegal char */
        sig++;
    }
    if (bad || sig % 4 != 0 || pad > 2) {
        mcs_throw(vm, EXC_FORMAT, "The input is not a valid Base-64 string as it contains a non-base 64 character, more than two padding characters, or an illegal character among the padding characters.");
        return mcs_null();
    }
    size_t n = sig / 4 * 3 - pad;
    mcs_value_t a = mcs_new_array(vm, (uint32_t)n);
    uint32_t acc = 0; int bits = 0; size_t k = 0;
    for (size_t i = 0; i < len && k < n; i++) {
        int v = b64_val(p[i]);
        if (v < 0) continue;
        acc = (acc << 6) | (uint32_t)v; bits += 6;
        if (bits >= 8) { bits -= 8; mcs_set_index(a, (uint32_t)k++, mcs_int((acc >> bits) & 0xFF)); }
    }
    return a;
}
NATIVE(cv_tohex) {   /* (bytes[, offset, length]) -> "0AFF" */
    uint32_t start, count;
    if (!span_args(vm, argc, argv, &start, &count)) return mcs_null();
    return hex_string(vm, argv[0], start, count, 0, hx_up);
}
NATIVE(cv_tohexlower) {
    uint32_t start, count;
    if (!span_args(vm, argc, argv, &start, &count)) return mcs_null();
    return hex_string(vm, argv[0], start, count, 0, hx_lo);
}
static int hex_val(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
NATIVE(cv_fromhex) {
    mcs_string_t* str = lib_need_str(vm, argv[0], "s"); CHECK();
    const uint8_t* p = (const uint8_t*)str->chars;
    size_t len = str->len;
    if (len % 2) { mcs_throw(vm, EXC_FORMAT, "The input is not a valid hex string as its length is not a multiple of 2."); return mcs_null(); }
    for (size_t i = 0; i < len; i++)
        if (hex_val(p[i]) < 0) { mcs_throw(vm, EXC_FORMAT, "The input is not a valid hex string as it contains a non-hex character."); return mcs_null(); }
    mcs_value_t a = mcs_new_array(vm, (uint32_t)(len / 2));
    for (size_t i = 0; i < len; i += 2) mcs_set_index(a, (uint32_t)(i / 2), mcs_int(hex_val(p[i]) << 4 | hex_val(p[i + 1])));
    return a;
}
static const mcs_reg_t convert_bytes_fns[] = {
    MCS_FN("ToBase64String", cv_tobase64, -1), MCS_FN("FromBase64String", cv_frombase64, 1),
    MCS_FN("ToHexString", cv_tohex, -1), MCS_FN("ToHexStringLower", cv_tohexlower, -1),
    MCS_FN("FromHexString", cv_fromhex, 1), MCS_REG_END
};
const mcs_reg_t* const mcs_lib_convert_bytes_fns = convert_bytes_fns;

/* ------------------------------------------------------------ lazy registration */
enum { LZ_ENCODING, LZ_ENCOBJ, LZ_BITCONV, LZ_BINPRIM };
const mcs_lib_entry_t mcs_lib_bytes_entries[] = {
    { "Encoding", LZ_ENCODING, MCS_LIB_ALL }, { "__Encoding", LZ_ENCOBJ, MCS_LIB_ALL },
    { "BitConverter", LZ_BITCONV, MCS_LIB_ALL }, { "BinaryPrimitives", LZ_BINPRIM, MCS_LIB_ALL },
    { NULL, 0, 0 }
};
void mcs_lib_bytes_make(mcs_vm_t* vm, int id) {
    mcs_class_t* c;
    switch (id) {
    case LZ_ENCODING:
        c = mcs_define_builtin_class(vm, "Encoding", NULL, CLS_STATIC);
        mcs_add_regs(vm, c, enc_fns, true);
        return;
    case LZ_ENCOBJ: lib_define_native_class(vm, &enc_def); return;
    case LZ_BITCONV:
        c = mcs_define_builtin_class(vm, "BitConverter", NULL, CLS_STATIC);
        mcs_add_regs(vm, c, bitconv_fns, true);
        return;
    case LZ_BINPRIM:
        c = mcs_define_builtin_class(vm, "BinaryPrimitives", NULL, CLS_STATIC);
        mcs_add_regs(vm, c, binprim_fns, true);
        return;
    default: return;
    }
}
#else
const mcs_lib_entry_t mcs_lib_bytes_entries[] = { { NULL, 0, 0 } };
void mcs_lib_bytes_make(mcs_vm_t* vm, int id) { (void)vm; (void)id; }
#endif /* MCS_ENABLE_BYTES */
