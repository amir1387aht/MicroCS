/* Copied retained arrays. Included only by the enabled binding; never pins VM
 * memory. Validate completely before native allocation, then publish under the
 * GUI lock. No fallible VM operation occurs between allocation and ownership. */
#if LV_USE_GRID || LV_USE_LINE
static bool buffer_sequence(mcs_vm_t* vm,mcs_value_t value,unsigned max,unsigned* count) {
    int k=mcs_obj_kind(value);
    if(k!=MCS_O_ARRAY && k!=MCS_O_LIST)return native_arg_error(vm,"LVGL buffer requires an integer array/list");
    uint32_t n=mcs_len(value);
    if(n>max)return native_arg_error(vm,"LVGL buffer exceeds configured capacity");
    *count=(unsigned)n;return true;
}
static bool buffer_integer(mcs_vm_t* vm,mcs_value_t value,int32_t* out) {
    if(!mcs_is_int(value))return native_arg_error(vm,"LVGL buffer elements must be integers");
    mcs_int_t n=mcs_to_int(vm,value);
    if(n<INT32_MIN || n>INT32_MAX)return native_arg_error(vm,"LVGL buffer integer is out of range");
    *out=(int32_t)n;return true;
}
/* All elements were validated; no allocation, conversion hooks or VM callbacks. */
static int32_t buffer_value(mcs_vm_t* vm,mcs_value_t value,unsigned i) {
    return (int32_t)mcs_to_int(vm,mcs_index(value,i));
}
#endif
#if LV_USE_GRID || LV_USE_LINE || LV_USE_TEXTAREA || LV_USE_DROPDOWN
static retained_buffer_t** buffer_link(session_t* s,handle_t* h,unsigned kind) {
    retained_buffer_t** link=&s->buffers;
    while(*link && !((*link)->slot==h->slot && (*link)->generation==h->generation && (*link)->kind==kind))link=&(*link)->next;
    return link;
}
static retained_buffer_t* buffer_alloc(session_t* s,handle_t* h,unsigned kind,size_t bytes) {
    retained_buffer_t* b=s->host.alloc(s->host.ctx,sizeof(*b)+bytes);
    if(b){b->next=NULL;b->slot=h->slot;b->generation=h->generation;b->kind=kind;b->split=b->count=0;}
    return b;
}
static void buffer_replace(session_t* s,handle_t* h,unsigned kind,retained_buffer_t* b) {
    retained_buffer_t** link=buffer_link(s,h,kind);retained_buffer_t* old=*link;
    if(b){b->next=old ? old->next : NULL;*link=b;}else if(old)*link=old->next;
    if(old)s->host.free(s->host.ctx,old);
}
#endif
#if LV_USE_GRID
/* LVGL 8.0 indexes track arrays without bounds checks. Validate every path that
 * changes cell placement, its parent, or the track array before native layout. */
static bool grid_fits(lv_obj_t* obj,unsigned cols,unsigned rows,int field,int32_t value) {
    int32_t c=field==1 ? value : lv_obj_get_style_grid_cell_column_pos(obj,0);
    int32_t cs=field==2 ? value : lv_obj_get_style_grid_cell_column_span(obj,0);
    int32_t r=field==3 ? value : lv_obj_get_style_grid_cell_row_pos(obj,0);
    int32_t rs=field==4 ? value : lv_obj_get_style_grid_cell_row_span(obj,0);
    return c>=0 && r>=0 && cs>=0 && rs>=0 && ((cs==0 || rs==0) || ((uint64_t)c+cs<=cols && (uint64_t)r+rs<=rows));
}
static retained_buffer_t* grid_for(session_t* s,lv_obj_t* parent) {
    if(!parent)return NULL;
    for(retained_buffer_t* b=s->buffers;b;b=b->next)if(b->kind==1) {
        slot_t* r=&s->slots[b->slot];if(r->generation==b->generation && r->obj==parent)return b;
    }
    return NULL;
}
static bool native_grid_bounds(session_t* s,lv_obj_t* parent,int32_t c,int32_t cs,int32_t r,int32_t rs) {
    retained_buffer_t* b=grid_for(s,parent);
    return !b || (c>=0 && r>=0 && cs>=0 && rs>=0 && ((cs==0 || rs==0) || ((uint64_t)c+cs<=b->split-1 && (uint64_t)r+rs<=b->count)));
}
static bool native_grid_existing(session_t* s,lv_obj_t* obj,lv_obj_t* parent,int field,int32_t value) {
    retained_buffer_t* b=grid_for(s,parent);return !b || grid_fits(obj,b->split-1,b->count,field,value);
}
#endif
NATIVE(grid_fraction) {
    (void)self;(void)argc;if(!session(vm))return mcs_null();
#if LV_USE_GRID
    int32_t n;if(!native_number_arg(vm,argv[0],&n,1,99,NARG_NUMBER))return mcs_null();
    return mcs_int(LV_GRID_FR(n)); /* 100 would equal TEMPLATE_LAST on both majors */
#else
    (void)argv;mcs_raise(vm,"NotSupportedException","Host LV_USE_GRID is disabled");return mcs_null();
#endif
}
NATIVE(set_grid) {
    (void)argc;
#if LV_USE_GRID
    handle_t* h=handle(vm,self);if(!h)return mcs_null();session_t* s=session(vm);if(!s)return mcs_null();
    unsigned cols,rows;
    if(!buffer_sequence(vm,argv[0],MCS_LVGL_MAX_GRID_TRACKS,&cols) || !buffer_sequence(vm,argv[1],MCS_LVGL_MAX_GRID_TRACKS,&rows))return mcs_null();
    if(!cols || !rows){native_arg_error(vm,"Grid needs at least one row and column");return mcs_null();}
    for(unsigned axis=0;axis<2;++axis)for(unsigned i=0;i<(axis ? rows : cols);++i) {
        int32_t n;if(!buffer_integer(vm,mcs_index(argv[axis],i),&n))return mcs_null();
        if(!((n>=1 && n<=4096) || n==LV_GRID_CONTENT || (n>=LV_GRID_FR(1) && n<=LV_GRID_FR(99)))) {
            native_arg_error(vm,"Grid track needs 1..4096 pixels, GRID_CONTENT or GridFraction(1..99)");return mcs_null();
        }
    }
    retained_buffer_t* b=buffer_alloc(s,h,1,sizeof(mcs_grid_coord_t)*(cols+rows+2));
    if(!b){mcs_raise(vm,"OutOfMemoryException","LVGL grid buffer allocation failed");return mcs_null();}
    b->split=cols+1;b->count=rows; mcs_grid_coord_t* tracks=(mcs_grid_coord_t*)b->data;
    for(unsigned i=0;i<cols;++i)tracks[i]=(mcs_grid_coord_t)buffer_value(vm,argv[0],i);
    tracks[cols]=LV_GRID_TEMPLATE_LAST;
    for(unsigned i=0;i<rows;++i)tracks[cols+1+i]=(mcs_grid_coord_t)buffer_value(vm,argv[1],i);
    tracks[cols+1+rows]=LV_GRID_TEMPLATE_LAST;
    s->host.lock(s->host.ctx);slot_t* r=lookup(s,h);bool fits=true;
    if(r)for(unsigned i=0;i<mcs_lv_child_count(r->obj);++i)if(!grid_fits(lv_obj_get_child(r->obj,(int32_t)i),cols,rows,0,0)){fits=false;break;}
    if(r && fits){lv_obj_set_grid_dsc_array(r->obj,tracks,tracks+b->split);buffer_replace(s,h,1,b);}else s->host.free(s->host.ctx,b);
    reap(vm,s);s->host.unlock(s->host.ctx);if(!r)invalid(vm);else if(!fits)native_arg_error(vm,"Grid tracks cannot exclude an existing child cell/span");return mcs_null();
#else
    (void)self;(void)argv;mcs_raise(vm,"NotSupportedException","Host LV_USE_GRID is disabled");return mcs_null();
#endif
}
NATIVE(set_points) {
    (void)argc;
#if LV_USE_LINE
    handle_t* h=handle(vm,self);if(!h)return mcs_null();session_t* s=session(vm);if(!s)return mcs_null();
    unsigned count;if(!buffer_sequence(vm,argv[0],2u*MCS_LVGL_MAX_LINE_POINTS,&count))return mcs_null();
    if(count%2 || count==2){native_arg_error(vm,"Line points need flat x,y pairs (empty or at least two points)");return mcs_null();}
    for(unsigned i=0;i<count;++i) {int32_t n;if(!buffer_integer(vm,mcs_index(argv[0],i),&n))return mcs_null();
        if(n < -4096 || n > 4096){native_arg_error(vm,"Line coordinates must be -4096..4096 pixels");return mcs_null();}}
    /* Check kind/staleness before allocation, revalidate after reacquiring lock. */
    s->host.lock(s->host.ctx);slot_t* r=lookup(s,h);bool wrong=r && r->kind!=19;s->host.unlock(s->host.ctx);
    if(!r){invalid(vm);return mcs_null();}if(wrong){mcs_raise(vm,"NotSupportedException","SetPoints requires Line");return mcs_null();}
    retained_buffer_t* b=count ? buffer_alloc(s,h,2,sizeof(mcs_line_point_t)*(count/2)) : NULL;
    if(count && !b){mcs_raise(vm,"OutOfMemoryException","LVGL line buffer allocation failed");return mcs_null();}
    if(b){b->count=count/2;mcs_line_point_t* points=(mcs_line_point_t*)b->data;for(unsigned i=0;i<count/2;++i){points[i].x=buffer_value(vm,argv[0],2*i);points[i].y=buffer_value(vm,argv[0],2*i+1);}}
    s->host.lock(s->host.ctx);r=lookup(s,h);
    if(r){lv_line_set_points(r->obj,b ? (const mcs_line_point_t*)b->data : NULL,count/2);buffer_replace(s,h,2,b);}else if(b)s->host.free(s->host.ctx,b);
    reap(vm,s);s->host.unlock(s->host.ctx);if(!r)invalid(vm);return mcs_null();
#else
    (void)self;(void)argv;mcs_raise(vm,"NotSupportedException","Host LV_USE_LINE is disabled");return mcs_null();
#endif
}
NATIVE(point_count) {
    UNUSED;
#if LV_USE_LINE
    BEGIN;unsigned count=0;bool wrong=r && r->kind!=19;
    if(r && !wrong){retained_buffer_t* b=*buffer_link(s,h,2);if(b)count=b->count;}
    s->host.unlock(s->host.ctx);if(!r)invalid(vm);else if(wrong)mcs_raise(vm,"NotSupportedException","PointCount requires Line");
    return mcs_int(count);
#else
    (void)self;mcs_raise(vm,"NotSupportedException","Host LV_USE_LINE is disabled");return mcs_null();
#endif
}

#if LV_USE_TEXTAREA || LV_USE_DROPDOWN
/* Native 8 / early 9 retain these pointers. Own one bounded copy; 9.5+
 * static setters prevent redundant native string allocations. */
static int native_retained_text(session_t* s,slot_t* r,unsigned kind,const char* text) {
    /* Empty filters mean unrestricted input in later LVGL, but reject all in
     * 8.0. Normalize empty/null to reset on both majors. */
    if(kind==3 && text && !text[0])text=NULL;
    handle_t h={(unsigned)(r-s->slots),r->generation};
    size_t n=text ? strlen(text) : 0;
    retained_buffer_t* b=text ? buffer_alloc(s,&h,kind,n+1) : NULL;
    if(text && !b)return NERR_MEMORY;
    const char* copy=NULL;
    if(b){memcpy(b->data,text,n+1);copy=(const char*)b->data;}
    switch(kind) {
#if LV_USE_TEXTAREA
    case 3:
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5
        lv_textarea_set_accepted_chars_static(r->obj,copy);
#else
        lv_textarea_set_accepted_chars(r->obj,copy);
#endif
        break;
#endif
#if LV_USE_DROPDOWN
    case 4:
#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 5
        lv_dropdown_set_text_static(r->obj,copy);
#else
        lv_dropdown_set_text(r->obj,copy);
#endif
        break;
    case 5:lv_dropdown_set_symbol(r->obj,copy);break;
#endif
    default:if(b)s->host.free(s->host.ctx,b);return NERR_UNAVAILABLE;
    }
    buffer_replace(s,&h,kind,b);return 0;
}
#endif
