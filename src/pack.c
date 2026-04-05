#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define MAGIC 0x54AF3B2C1E6D9A08ULL

/*
 * Output layout:
 *   [script header]        sh runs this; cmd jumps to :BATCH_START
 *   [base64 win binary]    cmd decodes this via powershell; sh skips (after exec)
 *   ": END_B64\n"          sentinel
 *   [linux binary raw]     sh extracts via dd
 *   [fat header 40 bytes]
 *
 * No raw binary bytes appear before the script's "exit /b", so cmd's
 * file-buffer pre-scan never hits a 0x1A (Ctrl-Z / EOF marker).
 *
 * The base64 block is valid ASCII so cmd reads through it safely.
 * powershell decodes it with [Convert]::FromBase64String().
 *
 * Placeholders (11 chars each, patched in-place):
 *   LINUX_OFF__  offset of linux binary from file start
 *   LINUX_SZ___  size of linux binary
 */

/* Script up to (not including) the base64 blob */
static const char script_top[] =
    ":; # 2>NUL & goto :BATCH_START\n"
    ":; set -e\n"
    ":; TMP=$(mktemp /tmp/run.XXXXXX)\n"
    ":; trap 'rm -f \"$TMP\"' EXIT\n"
    ":; dd if=\"$0\" bs=1 skip=LINUX_OFF__ count=LINUX_SZ___ 2>/dev/null > \"$TMP\"\n"
    ":; chmod +x \"$TMP\"\n"
    ":; exec \"$TMP\" \"$@\"\n"
    ":BATCH_START\n"
    "@echo off\n"
    "setlocal\n"
    "set TMP_OUT=%TEMP%\\run_%RANDOM%.exe\n"
    /* Decode the base64 block that follows this script header.
       We find it by reading this file, skipping to after ":BATCH_START\r\n@echo off\r\n..."
       -- easier: we just encode the whole blob between two sentinels and
       use findstr to extract it, then pipe to a temp b64 file, then decode. */
    "powershell -NoProfile -Command \""
        "$lines=[System.IO.File]::ReadAllLines('%~f0');"
        "$b64='';"
        "$in=$false;"
        "foreach($l in $lines){"
            "if($l -eq ':BEGIN_B64'){$in=$true;continue};"
            "if($l -eq ':END_B64'){break};"
            "if($in){$b64+=$l}"
        "};"
        "$bytes=[Convert]::FromBase64String($b64);"
        "[System.IO.File]::WriteAllBytes($env:TMP_OUT,$bytes)"
    "\"\n"
    "\"%TMP_OUT%\" %*\n"
    "set EC=%ERRORLEVEL%\n"
    "del \"%TMP_OUT%\"\n"
    "exit /b %EC%\n"
    ":BEGIN_B64\n";

static const char script_mid[] =
    ":END_B64\n"
    ": BINARY_DATA_BELOW\n";

/* base64 encoding table */
static const char b64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(FILE *out, const unsigned char *in, size_t len) {
    /* Emit base64 with lines prefixed by ":" so cmd treats them as labels
       (ignored), and sh never reaches them (already exec'd). */
    size_t col = 0;
    for (size_t i = 0; i < len; i += 3) {
        if (col == 0) {
            /* no colon prefix — they go between :BEGIN_B64 / :END_B64
               and powershell reads full lines stripping the newline.
               Plain base64 lines are fine; cmd won't execute them because
               it already jumped to :BATCH_START and will hit exit /b
               before :BEGIN_B64. */
        }
        unsigned char a = in[i];
        unsigned char b = (i+1 < len) ? in[i+1] : 0;
        unsigned char c = (i+2 < len) ? in[i+2] : 0;
        fputc(b64[a >> 2], out);
        fputc(b64[((a & 3) << 4) | (b >> 4)], out);
        fputc((i+1 < len) ? b64[((b & 15) << 2) | (c >> 6)] : '=', out);
        fputc((i+2 < len) ? b64[c & 63] : '=', out);
        col += 4;
        if (col >= 76) {
            fputc('\n', out);
            col = 0;
        }
    }
    if (col > 0) fputc('\n', out);
}

static unsigned char *read_file(const char *path, size_t *sz) {
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    *sz = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc(*sz);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, *sz, f);
    fclose(f);
    return buf;
}

static void patch_placeholder(char *buf, const char *needle, uint64_t value) {
    char *p = strstr(buf, needle);
    if (!p) {
        fprintf(stderr, "pack: placeholder '%s' not found\n", needle);
        exit(1);
    }
    char tmp[12];
    snprintf(tmp, sizeof(tmp), "%-11llu", (unsigned long long)value);
    memcpy(p, tmp, 11);
}

/* Calculate base64 encoded size (with newlines every 76 chars) */
static size_t b64_encoded_size(size_t raw) {
    size_t chars = ((raw + 2) / 3) * 4;          /* base64 chars */
    size_t newlines = (chars + 75) / 76;           /* one \n per 76-char line */
    return chars + newlines;
}

struct fat_header {
    uint64_t magic;
    uint64_t linux_offset;
    uint64_t win_offset;
    uint64_t win_size;
    uint64_t linux_size;
};

int main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr,
            "Usage: pack <linux_bin> <win_bin> <output.bat>\n"
            "\n"
            "  On Windows: output.bat\n"
            "  On Linux:   sh output.bat  (or chmod+x)\n");
        return 1;
    }

    size_t lin_sz, win_sz;
    unsigned char *lin_bin = read_file(argv[1], &lin_sz);
    unsigned char *win_bin = read_file(argv[2], &win_sz);
    if (!lin_bin || !win_bin) return 1;

    size_t top_len = sizeof(script_top) - 1;
    size_t mid_len = sizeof(script_mid) - 1;
    size_t b64_sz  = b64_encoded_size(win_sz);

    /* Linux binary starts after: script_top + b64 blob + script_mid */
    uint64_t linux_offset = (uint64_t)(top_len + b64_sz + mid_len);
    uint64_t linux_size   = (uint64_t)lin_sz;

    /* Patch script_top in place */
    char *top = malloc(top_len + 1);
    memcpy(top, script_top, top_len + 1);
    patch_placeholder(top, "LINUX_OFF__", linux_offset);
    patch_placeholder(top, "LINUX_SZ___", linux_size);

    FILE *out = fopen(argv[3], "wb");
    if (!out) { perror(argv[3]); return 1; }

    fwrite(top, 1, top_len, out);
    base64_encode(out, win_bin, win_sz);
    fwrite(script_mid, 1, mid_len, out);
    fwrite(lin_bin, 1, lin_sz, out);

    struct fat_header hdr;
    hdr.magic        = MAGIC;
    hdr.linux_offset = linux_offset;
    hdr.win_offset   = 0;        /* unused; win binary decoded from b64 */
    hdr.win_size     = win_sz;
    hdr.linux_size   = linux_size;
    fwrite(&hdr, 1, sizeof(hdr), out);
    fclose(out);

    free(lin_bin); free(win_bin); free(top);

    printf("Packed: %s  (b64_win=%zu  linux=%zu  linux_offset=%llu)\n",
           argv[3], b64_sz, lin_sz, (unsigned long long)linux_offset);
    return 0;
}
