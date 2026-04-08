#ifndef FILESWINDOWS
#define FILESWINDOWS

/* ─────────────────────────────────────────────────────────────── */
/* External ASM helpers                                            */
/* ─────────────────────────────────────────────────────────────── */
extern void* mem_alloc(unsigned long long size);
extern void  mem_free(void* p);
extern long  print_text(const char* s);
extern void  copy_string(char* d,const char* s);
extern void  mem_set(void* dst,int v,unsigned int n);
extern unsigned long my_strlen(const char* s);
extern char* my_strstr(char* h,const char* n);
extern void  u64_to_dec_left(char* out,unsigned long long v);

/* ─────────────────────────────────────────────────────────────── */
/* Win32 API imports                                               */
/* ─────────────────────────────────────────────────────────────── */

extern void* __stdcall CreateFileA(
    const char* lp_file_name,
    unsigned long dw_desired_access,
    unsigned long dw_share_mode,
    void* lp_security_attributes,
    unsigned long dw_creation_disposition,
    unsigned long dw_flags_and_attributes,
    void* h_template_file
);

extern long __stdcall ReadFile(
    void* h_file,
    void* lp_buffer,
    unsigned long n_bytes_to_read,
    unsigned long* lpBytesRead,
    void* lp_overlapped
);

extern long __stdcall WriteFile(
    void* h_file,
    const void* lp_buffer,
    unsigned long n_bytes_to_write,
    unsigned long* lp_bytes_written,
    void* lp_overlapped
);

extern long __stdcall CloseHandle(void* h_file);

extern long __stdcall GetFileSizeEx(
    void* h_file,
    void* lp_large_integer_out
);

/* ─────────────────────────────────────────────────────────────── */
/* Load entire file into memory                                    */
/* ─────────────────────────────────────────────────────────────── */

static char* load_text_file(const char* path,unsigned long* out_len) {
    void* h=CreateFileA(path,0x80000000UL,1,/* GENERIC_READ,FILE_SHARE_READ */
        0,3,0x80,0);/*OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL*/
    if (h==((void*)(long long)-1)) {
        return 0;
    }

    unsigned long long sz=0;
    GetFileSizeEx(h,&sz);

    char* buf=(char*)mem_alloc(sz+1);

    unsigned long got=0;
    ReadFile(h,buf,(unsigned long)sz,&got,0);

    buf[sz]=0;

    CloseHandle(h);
    *out_len=(unsigned long)sz;
    return buf;
}

/* ─────────────────────────────────────────────────────────────── */
/* Base64                                                          */
/* ─────────────────────────────────────────────────────────────── */

static const char b64tab[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(void* out,const unsigned char* in,unsigned long len) {
    unsigned long i,col=0;
    char line[4];
    unsigned char a,b,c;
    unsigned long tmp;

    for (i=0;i<len;i+=3) {
        a=in[i];
        b=(i+1<len)?in[i+1]:0;
        c=(i+2<len)?in[i+2]:0;

        line[0]=b64tab[a>>2];
        line[1]=b64tab[((a&3)<<4)|(b>>4)];
        line[2]=(i+1<len)?b64tab[((b&15)<< 2)|(c>>6)]:'=';
        line[3]=(i+2<len)?b64tab[c&63]:'=';

        WriteFile(out,line,4,&tmp,0);
        col+=4;

        if (col>=76) {
            WriteFile(out,"\n",1,&tmp,0);
            col=0;
        }
    }
    if (col>0) {
        WriteFile(out,"\n",1,&tmp,0);
    }
}

static unsigned long b64_encoded_size(unsigned long raw) {
    unsigned long chars=((raw+2)/3)*4;
    unsigned long newlines=chars/76;
    return chars+newlines+1;
}


#endif
