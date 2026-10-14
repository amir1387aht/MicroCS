from pathlib import Path
p=Path('src/mcs_internal.h');s=p.read_text();s=s.replace('typedef struct mcs_class {','#define MCS_CLASS_SHARED_LAYOUT 1u\n#define MCS_CLASS_PENDING_REGS  2u\n\ntypedef struct mcs_class {',1);s=s.replace('uint8_t layout_shared;  /* fields + field_defaults borrowed from super (copy on write) */','uint8_t layout_shared;  /* SHARED_LAYOUT / PENDING_REGS flags; no extra class storage */');p.write_text(s)
p=Path('src/mcs_object.c');s=p.read_text().replace('if (!cls->layout_shared) return;','if (!(cls->layout_shared & MCS_CLASS_SHARED_LAYOUT)) return;').replace('cls->layout_shared = 0;','cls->layout_shared &= (uint8_t)~MCS_CLASS_SHARED_LAYOUT;').replace('cls->layout_shared = 1;','cls->layout_shared |= MCS_CLASS_SHARED_LAYOUT;').replace('if (!c->layout_shared) {','if (!(c->layout_shared & MCS_CLASS_SHARED_LAYOUT)) {');old='''    if (mcs_table_get_s(t, name, out)) return true;
    /* A guarded first-use OOM can leave a module with completed earlier
     * registrations. Retry still-pending entries on a cold member miss, while
     * leaving the normal cached-member path unchanged. */
    if (mcs_lazy_pending(vm, c->name->chars, c->name->len)) {
        mcs_lazy_resolve(vm, c->name->chars, c->name->len);
        if (mcs_table_get_s(t, name, out)) return true;
    }
''';new='''    /* A partially applied module can already contain an older cached constant.
     * Complete its pending registrations before returning any member. Ordinary
     * classes have no flag, so their cached path never scans the pending queue. */
    if ((c->layout_shared & MCS_CLASS_PENDING_REGS) &&
        mcs_lazy_pending(vm, c->name->chars, c->name->len))
        mcs_lazy_resolve(vm, c->name->chars, c->name->len);
    if (mcs_table_get_s(t, name, out)) return true;
''';assert old in s;s=s.replace(old,new);p.write_text(s)
p=Path('src/mcs_lib.c');s=p.read_text();s=s.replace('''        mcs_class_t* c = module_for(vm, z->name);
        if (z->regs)''','''        mcs_class_t* c = module_for(vm, z->name);
        c->layout_shared |= MCS_CLASS_PENDING_REGS;
        if (z->regs)''',1);old='''    vm->gc_pause--;
    return any;
}

void mcs_lazy_free''';new='''    /* An outer, currently applying registration still owns this flag. Do not
     * clear it merely because recursive lookup skips that active queue entry. */
    bool queued = false;
    for (mcs_lazy_t* z = vm->lazy; z; z = z->next)
        if (name_is(z->name, name, len)) { queued = true; break; }
    if (!queued) {
        mcs_class_t* c = peek_global_class(vm, name);
        if (c) c->layout_shared &= (uint8_t)~MCS_CLASS_PENDING_REGS;
    }
    vm->gc_pause--;
    return any;
}

void mcs_lazy_free''';assert old in s;s=s.replace(old,new,1);p.write_text(s)
p=Path('tests/c/test_lazy_recovery.c');s=p.read_text();s=s.replace('static mcs_value_t third_failure','static mcs_value_t second_failure(mcs_vm_t *vm, mcs_value_t self, int argc, mcs_value_t *argv) {\n    (void)vm; (void)self; (void)argc; (void)argv; fail_next=2; return mcs_null();\n}\nstatic mcs_value_t third_failure',1);s=s.replace('static const mcs_class_def_t deep_def=', 'static const mcs_class_def_t partial_def={"PartialLateOwned",sizeof(int),owned_ctor,NULL,members,NULL};\nstatic const mcs_class_def_t deep_def=',1);s=s.replace('mcs_register_function(vm,"FailThird",third_failure,0);','mcs_register_function(vm,"FailThird",third_failure,0);\n    mcs_register_function(vm,"FailSecond",second_failure,0);');s=s.replace('mcs_register_class(vm,&nested_def);','mcs_register_class(vm,&nested_def); mcs_register_class(vm,&partial_def);');s=s.replace('CHECK(partial_name && partial_name->obj.aux && vm->globals[partial_name->obj.aux-1].type==MCS_T_UNDEF);','''/* Allocation three is the instance, after the complete class is published. */
    CHECK(partial_name && partial_name->obj.aux && IS_KIND(vm->globals[partial_name->obj.aux-1],MCS_O_CLASS) && AS_CLASS(vm->globals[partial_name->obj.aux-1])->def==&deep_def);''');needle='''#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    const int entry_pause''';new='''#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    /* Allocation two is ROM registration metadata, before class publication. */
    CHECK(mcs_exec_source(vm,"metadata-class.cs","FailSecond(); var metadataUse=new PartialLateOwned();") == MCS_ERR_MEMORY);
    CHECK(fail_next==0);
    mcs_string_t *metadata_name=mcs_find_interned(vm,partial_def.name,strlen(partial_def.name));
    CHECK(metadata_name && metadata_name->obj.aux && vm->globals[metadata_name->obj.aux-1].type==MCS_T_UNDEF);
    CHECK(constructions==3);
#endif
    CHECK(mcs_exec_source(vm,"metadata-retry.cs","var metadataRecovered=new PartialLateOwned(); if(metadataRecovered.Value!=42) throw new Exception(\\"metadata retry\\");") == MCS_OK);
#if MCS_LAZY_CLASSES && MCS_LAZY_REGS
    const int entry_pause''';assert needle in s;s=s.replace(needle,new,1).replace('CHECK(constructions==5);','CHECK(constructions==6);');p.write_text(s)
