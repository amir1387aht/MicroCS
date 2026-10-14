/* Independent ordinary RAM oracle through public VM calls and REAL TFT_eSPI.
 * No physical initialization, hostile images, replacement pixels or suppressions. */
#include "TFT_eSPI.h"
#include "mcs.h"
#include "mcs_hal.h"
#include "mcs_tft.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
TestSerial Serial;
SPIClass SPI;
static size_t requested;
static int checks, failed, cases, compared;
extern "C" void *__real_calloc(size_t, size_t);
extern "C" void *__wrap_calloc(size_t n, size_t s) { return __real_calloc(n, s); }
static void *allocate(void *, void *p, size_t old, size_t n) {
    if (!n) { free(p); requested -= old; return nullptr; }
    void *next = realloc(p, n);
    if (next) requested = requested - old + n;
    return next;
}
static void quiet(void *, const char *, size_t) {}
static void check(bool ok, const char *why) {
    ++checks;
    if (!ok) { ++failed; fprintf(stderr, "FAIL %s\n", why); }
}
static mcs_vm_t *create() {
    mcs_config_t cfg; mcs_config_default(&cfg);
    cfg.realloc_fn = allocate; cfg.write_fn = quiet; cfg.error_fn = quiet;
    mcs_vm_t *vm = mcs_new(&cfg); check(vm != nullptr, "VM creation"); return vm;
}
static bool run(mcs_vm_t *vm, const char *code) {
    mcs_result_t r = mcs_exec_source(vm, "review-scroll.cs", code);
    check(r == MCS_OK, "ordinary C# execution");
    if (r != MCS_OK) fprintf(stderr, "%s\n", mcs_last_error(vm));
    return r == MCS_OK;
}
static mcs_result_t call(mcs_vm_t *vm, mcs_value_t obj, const char *method,
                         std::initializer_list<int> ints, mcs_value_t *out = nullptr) {
    std::vector<mcs_value_t> args; for (int n : ints) args.push_back(mcs_int(n));
    return mcs_invoke(vm, obj, method, (int)args.size(), args.data(), out);
}
static int pixel(mcs_vm_t *vm, mcs_value_t obj, int x, int y) {
    mcs_value_t value;
    auto r = call(vm, obj, "ReadValue", {x,y}, &value);
    check(r == MCS_OK && value.type == MCS_T_INT, "native raw pixel read");
    return r == MCS_OK && value.type == MCS_T_INT ? (int)value.as.i : -1;
}
static int quantized(int depth, int color) {
    return depth == 8 ? ((color & 0xE000) >> 8) | ((color & 0x0700) >> 6) |
                        ((color & 0x0018) >> 3) : color;
}
static void oracle() {
    struct rect { int x,y,w,h; };
    const rect rectangles[] = {{0,0,11,7},{1,1,9,5},{0,2,1,5},{0,6,11,1},{10,0,1,7},{3,2,5,3}};
    for (int depth : {1,4,8,16}) {
        mcs_vm_t *vm = create(); if (!vm) return;
        char setup[240]; snprintf(setup,sizeof setup,
            "var reviewOwner=new Tft(); var reviewSprite=new TftSprite(reviewOwner,11,7,%d);",depth);
        if (!run(vm, setup)) { mcs_free(vm); continue; }
        mcs_value_t sprite = mcs_get_global(vm,"reviewSprite"); // Script global roots receiver.
        if (depth == 4) check(call(vm,sprite,"SetPaletteColor",{7,0x1234})==MCS_OK,"custom palette");
        if (depth == 1) check(call(vm,sprite,"BitmapColors",{0xF800,0x001F})==MCS_OK,"custom bitmap colors");
        const int fill = depth == 1 ? 1 : depth == 4 ? 7 : 0xA35C;
        for (const auto &r : rectangles) {
            for (int dx : {-4096,-r.w,-1,0,1,r.w,4096}) for (int dy : {-r.h,-1,0,1,r.h}) {
                std::vector<int> before(77);
                for (int i=0;i<77;i++) {
                    int value = depth == 1 ? (i*7+i/11)%2 : depth == 4 ? (i*7+i/11)%16 : (i*811+37)%65536;
                    check(call(vm,sprite,"DrawPixel",{i%11,i/11,value})==MCS_OK,"native pattern write");
                    before[i]=pixel(vm,sprite,i%11,i/11);
                }
                mcs_gc(vm); // Retained script global and common GUI/display owner must survive.
                check(call(vm,sprite,"ScrollRect",{r.x,r.y,r.w,r.h,dx,dy,fill})==MCS_OK,"scroll invocation");
                for (int y=0;y<7;y++) for (int x=0;x<11;x++) {
                    int expected=before[y*11+x];
                    if (x>=r.x && x<r.x+r.w && y>=r.y && y<r.y+r.h) {
                        const int sx=x-dx, sy=y-dy;
                        expected=sx>=r.x && sx<r.x+r.w && sy>=r.y && sy<r.y+r.h ? before[sy*11+sx] : quantized(depth,fill);
                    }
                    int actual=pixel(vm,sprite,x,y); ++compared;
                    if (actual!=expected) fprintf(stderr,"depth%d rect%d,%d,%d,%d dx%d dy%d at%d,%d expected%d actual%d\n",depth,r.x,r.y,r.w,r.h,dx,dy,x,y,expected,actual);
                    check(actual==expected,"immutable snapshot oracle / outside unchanged");
                }
                ++cases;
            }
        }
        // Every invalid call independently checked for unchanged patterned pixels.
        std::vector<int> before(77); for(int i=0;i<77;i++) before[i]=pixel(vm,sprite,i%11,i/11);
        for (auto bad : {rect{-1,0,1,1},rect{0,0,0,1},rect{10,0,2,1},rect{0,6,1,2}}) {
            check(call(vm,sprite,"ScrollRect",{bad.x,bad.y,bad.w,bad.h,1,0,fill})==MCS_ERR_RUNTIME,"invalid rectangle rejected");
            for(int i=0;i<77;i++) check(pixel(vm,sprite,i%11,i/11)==before[i],"invalid rectangle is nonmutating");
        }
        check(call(vm,sprite,"ScrollRect",{0,0,1,1,4097,0,fill})==MCS_ERR_RUNTIME,"displacement narrowing rejected");
        for(int i=0;i<77;i++) check(pixel(vm,sprite,i%11,i/11)==before[i],"invalid displacement is nonmutating");
        run(vm,"reviewSprite.Dispose(); reviewOwner.Dispose();");
        check(call(vm,sprite,"ScrollRect",{0,0,1,1,0,0,fill})==MCS_ERR_RUNTIME,"disposed zero movement rejected");
        mcs_free(vm); check(requested==0,"VM-requested teardown complete");
    }
}
static const char image_source[] =
    "var p=new Tft(); var s=new TftSprite(p,3,2,4); s.SetPaletteColor(7,0x1234); "
    "s.Fill(3); s.DrawPixel(0,0,7); s.ScrollRect(0,0,3,2,1,1,0); "
    "if(s.ReadValue(1,1)!=7 || s.ReadValue(2,1)!=3 || s.ReadValue(0,0)!=0 || "
    "s.ReadPixel(1,1)!=0x1234) throw new Exception(\"source/image snapshot\");";
static void image_tests() {
    mcs_vm_t *vm=create(); if(vm) { run(vm,image_source); mcs_gc(vm); mcs_free(vm); check(requested==0,"undisposed source cleanup"); }
    for(unsigned flags : {0u,MCS_IMAGE_NO_OPT}) for(bool xip : {false,true}) {
        mcs_vm_t *compiler=create(); if(!compiler) return;
        uint8_t *image=nullptr; size_t size=0;
        check(mcs_compile_image_ex(compiler,"review-scroll-image.cs",image_source,flags,&image,&size)==MCS_OK,"trusted source image compilation");
        if(image) {
            vm=create();
            if(vm) { auto result=xip?mcs_exec_image_xip(vm,image,size):mcs_exec_image(vm,image,size); check(result==MCS_OK,"compiler-generated copied/XIP execution"); if(result!=MCS_OK) fprintf(stderr,"%s\n",mcs_last_error(vm)); mcs_gc(vm); mcs_free(vm); }
            mcs_free_image(compiler,image); // XIP borrower already destroyed; input stays valid until then.
        }
        mcs_free(compiler); check(requested==0,"compiler/image/userdata teardown");
    }
}
static void lifetime_tests() {
    for(int repeat=0;repeat<6;repeat++) {
        mcs_vm_t *vm=create(); if(!vm) return;
        run(vm,"var p=new Tft(); var s=new TftSprite(p,11,7,4); s.Fill(3);");
        auto checkpoint=mcs_tft_scope_begin(vm);
        run(vm,"s.ScrollRect(1,1,9,5,1,-1,7);");
        check(mcs_tft_scope_end(vm,checkpoint)==0,"scope preserves older embedding resources");
        mcs_gc(vm); run(vm,"if(s.ReadValue(1,5)!=7) throw new Exception(\"persistent scroll\");");
        mcs_limits_t limits={};limits.steps=1200;mcs_set_limits(vm,&limits);
        check(mcs_exec_source(vm,"review-abort.cs","while(true) { s.ScrollRect(0,0,11,7,1,0,0); }")==MCS_ERR_ABORTED,"ordinary source budget cancellation");
        mcs_set_limits(vm,nullptr);run(vm,"s.ScrollRect(0,0,11,7,0,0,0); s.Dispose(); p.Dispose();");
        checkpoint=mcs_tft_scope_begin(vm);
        check(mcs_exec_source(vm,"review-error.cs","var q=new Tft(); var t=new TftSprite(q,11,7,1); t.ScrollRect(1,1,9,5,-1,1,1); throw new Exception(\"ordinary review unwind\");")==MCS_ERR_RUNTIME,"ordinary source exception");
        check(mcs_tft_scope_end(vm,checkpoint)==0 && mcs_tft_scope_end(vm,checkpoint)==0,"scope cleanup idempotent after scroll/error");
        run(vm,"try { t.ScrollRect(0,0,1,1,0,0,0); throw new Exception(\"stale child\"); } catch(ObjectDisposedException e) {} var replacement=new Tft(); replacement.Dispose();");
        mcs_gc(vm);mcs_free(vm);check(requested==0,"error/abort/GC lifetime teardown");
    }
}
int main() {
    oracle(); image_tests(); lifetime_tests();
    printf("%d/%d independent checks; %d immutable snapshot cases, %d pixel comparisons; compiler source/copy/XIP and lifetime tests; NO physical IO\n",checks-failed,checks,cases,compared);
    return failed?1:0;
}
