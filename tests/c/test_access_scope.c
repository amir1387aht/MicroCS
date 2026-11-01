/* Generic language regressions; no device/application fixtures. */
#include "mcs.h"
#include <stdio.h>
#include <string.h>
static int checks, failed;
static void quiet(void* ud, const char* s, size_t n) { (void)ud; (void)s; (void)n; }
static void check(const char* name, const char* source, int valid, const char* error) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.write_fn = quiet;
    mcs_vm_t* vm = mcs_new(&cfg);
    checks++;
    if (!vm) { failed++; return; }
    mcs_result_t r = mcs_check_source(vm, name, source);
    if ((valid && r != MCS_OK) || (!valid && (r != MCS_ERR_COMPILE || (error && !strstr(mcs_last_error(vm), error))))) {
        failed++; fprintf(stderr,"FAIL %s: %d %s\n",name,r,mcs_last_error(vm));
    }
    if (valid && mcs_exec_source(vm, name, source) != MCS_OK) { failed++; fprintf(stderr,"FAIL run %s: %s\n",name,mcs_last_error(vm)); }
    mcs_free(vm);
}
#define BAD(name,src) check(name,src,0,"protection level")
#define GOOD(name,src) check(name,src,1,NULL)
int main(void) {
    BAD("private-field", "class Box { private int secret=7; } var b=new Box(); int x=b.secret;");
    BAD("default-field", "class Box { int secret=7; } var b=new Box(); int x=b.secret;");
    BAD("private-method", "class Box { private int Secret()=>7; } var b=new Box(); int x=b.Secret();");
    BAD("private-static", "class Box { private static int Secret()=>7; } int x=Box.Secret();");
    BAD("private-constant", "class Box { private const int Secret=7; } int x=Box.Secret;");
    BAD("private-constructor", "class Box { private Box(){} } var b=new Box();");
    BAD("private-optional-constructor", "class Box { private Box(int x=1){} } var b=new Box();");
    BAD("private-getter-compound", "class Box { public int Value { private get; set; } } var b=new Box(); b.Value+=1;");
    BAD("private-getter-increment", "class Box { public int Value { private get; set; } } var b=new Box(); b.Value++;");
    BAD("private-setter", "class Box { public int Value { get; private set; } } var b=new Box(); b.Value=7;");
    BAD("private-getter", "class Box { public int Value { private get; set; } } var b=new Box(); int x=b.Value;");
    BAD("private-assignment", "class Box { private int secret; } var b=new Box(); b.secret=7;");
    BAD("private-increment", "class Box { private int secret; } var b=new Box(); b.secret++;");
    BAD("private-initializer", "class Box { private int secret; } var b=new Box { secret=7 };");
    BAD("private-base", "class Box { private int secret; } class Child:Box { public int Read()=>secret; }");
    BAD("private-base-explicit", "class Box { private int Secret()=>7; } class Child:Box { public int Read()=>base.Secret(); }");
    BAD("private-typed-param", "class Box { private int secret; } class Reader { public int Read(Box b)=>b.secret; }");
    BAD("private-captured", "class Box { private int secret; } var b=new Box(); Func<int> read=()=>b.secret;");
    BAD("private-return", "class Box { private int secret; } class Factory { public Box Make()=>new Box(); } var f=new Factory(); int x=f.Make().secret;");
    BAD("protected-external", "class Box { protected int secret; } var b=new Box(); int x=b.secret;");
    GOOD("same-class", "class Box { private int secret=7; public int Read(Box b)=>b.secret; } var b=new Box(); int x=b.Read(b);");
    GOOD("private-internal-call", "class Box { private int Secret()=>7; public int Read()=>Secret(); } var b=new Box(); int x=b.Read();");
    GOOD("protected-derived", "class Box { protected int secret=7; } class Child:Box { public int Read()=>base.secret; } var b=new Child(); int x=b.Read();");
    GOOD("private-setter-internal", "class Box { public int Value { get; private set; } public void Set(){Value=7;} } var b=new Box(); b.Set(); int x=b.Value;");
    GOOD("private-getter-public-set", "class Box { public int Value { private get; set; } } var b=new Box(); b.Value=7;");
    GOOD("namespace-import", "using Library; namespace Library { class Box { public static int Value=7; } } int x=Box.Value;");
    GOOD("namespace-qualified", "namespace Library { class Box { public int Value=7; } } var b=new Library.Box(); int x=b.Value;");
    GOOD("namespace-static-qualified", "namespace Library { class Box { public static int Value=7; } } int x=Library.Box.Value;");
    GOOD("namespace-same", "namespace Library { class Box { public static int Value=7; } class Reader { public static int Read()=>Box.Value; } } int x=Library.Reader.Read();");
    GOOD("namespace-nested", "namespace Library { namespace Nested { class Box { public static int Value=7; } } } int x=Library.Nested.Box.Value;");
    GOOD("namespace-two-same-names", "namespace A { class Box { public int Value=1; } } namespace B { class Box { public int Value=2; } } var a=new A.Box(); var b=new B.Box(); if(a is B.Box) throw new Exception(\"type identity\"); if(!(a is A.Box)) throw new Exception(\"type identity\"); if(a.GetType().Name!=\"Box\") throw new Exception(\"short name\"); if(a.GetType().FullName!=\"A.Box\") throw new Exception(\"full name\");");
    check("namespace-missing-import", "namespace Library { class Box { public static int Value=7; } } int x=Box.Value;", 0,"not in scope");
    check("namespace-missing-constructor-import", "namespace Library { class Box {} } var b=new Box();", 0,"not in scope");
    check("namespace-ambiguous", "using A; using B; namespace A { class Box {} } namespace B { class Box {} } var b=new Box();", 0,"ambiguous type");
    GOOD("qualified-trivia", "namespace Library { class Box { public int Value=7; } } var b=new Library /* comment */ . Box(); int x=b.Value;");
    GOOD("qualified-primitive", "System /* comment */ . Int32 x=7; if(x!=7)throw new Exception(\"primitive alias\");");
    GOOD("parent-namespace", "namespace A { class Box { public static int Value=7; } } namespace A.B { class Reader { public static int Read()=>Box.Value; } } int x=A.B.Reader.Read();");
    GOOD("native-qualified", "System.Console.WriteLine(7); var x=new System.Text.StringBuilder(); x.Append(\"ok\");");
    printf("%s access/scope: %d cases, %d failures\n", failed?"FAIL":"PASS",checks,failed);
    return failed ? 1 : 0;
}
