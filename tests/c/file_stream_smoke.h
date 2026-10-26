/* Shared public API regression, run on the actual mounted backend. */
static void stream_smoke_out(void* ud, const char* p, size_t n) { (void)ud; fwrite(p, 1, n, stderr); }
static int file_stream_smoke(mcs_vfs_t* vfs, const char* root, bool random_write) {
    mcs_config_t cfg; mcs_config_default(&cfg); cfg.write_fn = stream_smoke_out; cfg.error_fn = stream_smoke_out;
    mcs_vm_t* vm = mcs_new(&cfg); if (!vm) return -1;
    mcs_fs_open_lib(vm, vfs); mcs_set_global(vm, "Root", mcs_string(vm, root)); mcs_set_global(vm, "RandomWrite", mcs_bool(random_write));
    int r = mcs_exec_source(vm, "stream-smoke.cs",
        "void Check(bool ok) { if(!ok)throw new Exception(\"stream smoke\"); }"
        "string path=Root+\"/stream-smoke.bin\";byte[] a=new byte[150];for(int i=0;i<150;i++)a[i]=i;"
        "var w=File.Create(path);w.Write(a,0,150);w.Flush();Check(w.Length==150);w.Dispose();"
        "var f=File.OpenRead(path);byte[] b=new byte[50];Check(f.Read(b,0,50)==50&&b[49]==49);"
        "Check(f.Read(b,0,50)==50&&b[0]==50);f.Position=125;Check(f.Read(b,0,50)==25&&b[24]==149);"
        "Check(f.ReadByte()==-1);Check(f.Seek(-100,SeekOrigin.Current)==50&&f.ReadByte()==50);f.Close();"
        "var ap=new FileStream(path,FileMode.Append);ap.WriteByte(200);ap.Close();"
        "Check(File.GetLength(path)==151);"
        "if(RandomWrite){var rw=File.OpenWrite(path);rw.Position=50;rw.WriteByte(201);rw.Close();"
        "f=File.OpenRead(path);f.Position=50;Check(f.ReadByte()==201);f.Close();}"
        "File.Delete(path);");
    mcs_free(vm); return r;
}
