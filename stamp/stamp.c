#define WINDOWS_STAT_TIMESPEC 1
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>

#define VERSION "0.1.3"

#ifdef _WIN32 // WINDOWS
#include <windows.h>
#include <fileapi.h>
#include <direct.h>
#include <sys/utime.h>

#define fopen fopen_compat
FILE *fopen_compat(const char *filename, const char *mode){
    FILE *f = NULL;
    errno_t x = fopen_s(&f, filename, mode);
    // the global errno is also set, so we can discard this.
    (void)x;
    return f;
}

int update_timestamp(const char *path){
    return _utime(path, NULL);
}

#else // UNIX
#include <utime.h>

int update_timestamp(const char *path){
    return utime(path, NULL);
}

#endif

// string_t

typedef struct {
    size_t len;
    const char *text;
} string_t;

static int string_cmp(const string_t a, const string_t b){
    size_t n = a.len < b.len ? a.len : b.len;
    int cmp = strncmp(a.text, b.text, n);
    if(cmp) return cmp;
    // n-length substrings match, return -1 if a is shorter, 1 if b is shorter
    if(a.len < b.len) return -1;
    return (int)(b.len < a.len);
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

static string_t string_sub(const string_t in, size_t start, size_t end){
    // decide start-offset
    size_t so = MIN(in.len, start);
    // decide end-offset
    size_t eo = MIN(in.len, end);

    // don't let eo < so
    eo = MAX(so, eo);

    return (string_t){ .text = in.text + so, .len = eo - so };
}

// code copied from splintermail project, then modified
// (https://github.com/splintermail/splintermail-client)
// original code was public domain under the UNLICENSE

static const string_t DOT = { .text = ".", .len = 1 };

static bool _is_sep(char c){
    #ifndef _WIN32 // UNIX
    return c == '/';
    #else // WINDOWS
    return c == '/';
    // allowing backslash as a separator breaks our escape parsing
    // return c == '/' || c == '\\';
    #endif
}

// returns the number of trailing seps at the end of path, excluding tail
static size_t _get_trailing_sep(const string_t path, size_t tail){
    size_t out = 0;
    for(; out + tail < path.len; out++){
        if(!_is_sep(path.text[path.len - tail - 1 - out])) return out;
    }
    return out;
}

// returns the number of trailing non-seps at the end of path, excluding tail
static size_t _get_trailing_nonsep(const string_t path, size_t tail){
    size_t out = 0;
    for(; out + tail < path.len; out++){
        if(_is_sep(path.text[path.len - tail - 1 - out])) return out;
    }
    return out;
}

#ifdef _WIN32 // WINDOWS

// returns number of seps at start of path, exlcuding start
static size_t _get_sep(const string_t path, size_t start){
    size_t out = 0;
    for(; start + out < path.len; out++){
        if(!_is_sep(path.text[start + out])) return out;
    }
    return out;
}

// returns number of non-seps at start of path, exlcuding start
static size_t _get_non_sep(const string_t path, size_t start){
    size_t out = 0;
    for(; start + out < path.len; out++){
        if(_is_sep(path.text[start + out])) return out;
    }
    return out;
}

// returns 0 if not found, 2 for relative, 3 for absolute
static size_t _get_letter_drive(
    const string_t path, size_t start, bool colon, bool include_sep
){
    if(start > path.len || path.len - start < 2) return 0;
    char c = path.text[start];
    if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) return 0;
    if(path.text[start + 1] != (colon ? ':' : '$')) return 0;
    if(include_sep && path.len - start > 2 && _is_sep(path.text[start + 2])){
        return 3;
    }
    return 2;
}

// returns 0 if not found, else (3 + one or more trailing slashes)
static size_t _get_dos_device_indicator(const string_t path){
    if(path.len < 4) return 0;
    if(!_is_sep(path.text[0])) return 0;
    if(!_is_sep(path.text[1])) return 0;
    char c = path.text[2];
    if(c != '.' && c != '?') return 0;
    size_t seps = _get_sep(path, 3);
    if(!seps) return 0;
    return 3 + seps;
}

// returns 0 if not found, else 2
static size_t _get_unc_indicator(const string_t path){
    size_t sep = _get_sep(path, 0);
    return sep == 2 ? 2 : 0;
}

static bool string_ieq(string_t a, string_t b){
    return a.len == b.len && _strnicmp(a.text, b.text, a.len) == 0;
}

// return 0 or (3 + one or more trailing slashes)
static size_t _get_dos_unc_indicator(const string_t path, size_t start){
    static const string_t UNC = { .text = "unc", .len = 3 };
    string_t unc = string_sub(path, start, start + 3);
    if(!string_ieq(unc, UNC)) return 0;
    size_t seps = _get_sep(path, start + 3);
    if(!seps) return 0;
    return 3 + seps;
}

// returns 0 if not found or the text including server\share or server\c$[\]
static size_t _get_unc(const string_t path, size_t start){
    size_t server = _get_non_sep(path, start);
    if(!server) return 0;
    size_t sep = _get_sep(path, start + server);
    if(!sep) return 0;
    // check for the server\c$ case
    size_t drive = _get_letter_drive(path, start + server + sep, false, false);
    if(drive) return (start + server + sep);
    // otherwise expect the server\share case
    size_t share = _get_non_sep(path, start + server + sep);
    if(!share) return 0;
    return server + sep + share;
}

#endif

/* read the atomic part of a path string, the part which is unmodified by both
   the dirname and the basename.  In Unix, that's just a leading '/'.  In
   Windows, it takes many forms:
    - C:                    drive letter (relative path form)
    - C:/                   drive letter (absolute path form)

    - \\server\share        a UNC path to a shared directory
    - \\server\C$           a UNC path to a drive
                              (the docs reference some sort of relative
                               version, but that seems like a myth; I can't
                               `ls` any relative version in powershell)

    - \\.\VOL               a DOS device path (VOL could be C: or Volume{UUID})
    - \\?\VOL               another form of DOS device path
    - \\.\UNC\server\share  a DOS device to a UNC path to a shared directory
    - \\.\UNC\server\C$     a DOS device to a UNC path to a drive */
static size_t _get_volume(const string_t path){
#ifdef _WIN32 // WINDOWS
    // letter-drive case: C:
    size_t letter_drive = _get_letter_drive(path, 0, true, true);
    if(letter_drive){
        return letter_drive;
    }

    // DOS device case: \\.\UNC or \\.\DRIVE
    size_t dos_dev = _get_dos_device_indicator(path);
    if(dos_dev){
        // DOS-UNC case
        size_t dos_unc_indicator = _get_dos_unc_indicator(path, dos_dev);
        if(dos_unc_indicator){
            size_t unc = _get_unc(path, dos_dev + dos_unc_indicator);
            if(!unc) return 0;
            return dos_dev + dos_unc_indicator + unc;
        }
        // DOS-VOLUME case, don't bother checking the volume
        size_t volume = _get_non_sep(path, dos_dev);
        if(!volume) return 0;
        return dos_dev + volume;
    }

    // UNC case: \\server\share
    size_t unc_indicator = _get_unc_indicator(path);
    if(unc_indicator){
        size_t unc = _get_unc(path, unc_indicator);
        if(!unc) return 0;
        return unc_indicator + unc;
    }
#endif

    // absolute path
    if(path.len && _is_sep(path.text[0])) return 1;
    return 0;
}

static string_t _get_path_part(const string_t path, bool wantdir){
    // special case: empty string
    if(path.len == 0) return DOT;
    // special case: "."
    if(string_cmp(path, DOT) == 0) return path;

    // we never break down the volume and we never include it
    size_t volume = _get_volume(path);
    string_t nonvol = string_sub(path, volume, path.len);

    // we never consider a trailing sep
    size_t tsep = _get_trailing_sep(nonvol, 0);

    // special case: only the volume [+trailing sep]
    if(tsep == nonvol.len) return string_sub(path, 0, volume);

    // drop the base and the joiner for the base (if any)
    size_t base = _get_trailing_nonsep(nonvol, tsep);
    size_t joiner = _get_trailing_sep(nonvol, tsep + base);
    size_t dir = nonvol.len - tsep - base - joiner;
    if(wantdir){
        // special case: just a basename
        if(volume + dir == 0){
            return DOT;
        }
        return string_sub(path, 0, volume + dir);
    }else{
        return string_sub(
            path, volume + dir + joiner, volume + dir + joiner + base
        );
    }
}

string_t ddirname(const string_t path){
    return _get_path_part(path, true);
}

// end of splintermail code

int exists(const char *path, bool *ok){
    struct stat s;
    int ret = stat(path, &s);
    if(ret != 0){
        *ok = false;
        if(errno == ENOENT || errno == ENOTDIR) return 0;
        perror(path);
        return 1;
    }

    *ok = true;
    return 0;
}

int exists_string(const string_t s, bool *ok){
    *ok = false;
    // create null-terminated string
    char mem[4096];
    if(s.len >= sizeof(mem)){
        fprintf(stderr, "path too long!\n");
        return 1;
    }
    memcpy(mem, s.text, s.len);
    mem[s.len] = '\0';

    int ret = exists(mem, ok);
    if(ret) perror(mem);
    return ret;
}

int mkdir_string(const string_t s){
    // create null-terminated string
    char mem[4096];
    if(s.len >= sizeof(mem)){
        fprintf(stderr, "path too long!\n");
        return 1;
    }
    memcpy(mem, s.text, s.len);
    mem[s.len] = '\0';

    #ifdef _WIN32
    int ret = _mkdir(mem);
    #else
    int ret = mkdir(mem, 0777);
    #endif
    if(ret) perror(mem);
    return ret;
}

int mkdirs(string_t tgt){
    // check if it already exists
    bool ok;
    int ret = exists_string(tgt, &ok);
    if(ret) return ret;
    if(ok) return 0;

    // create parent if needed
    string_t parent = ddirname(tgt);
    if(string_cmp(parent, tgt) != 0){
        ret = mkdirs(parent);
        if(ret) return ret;
    }

    // create tgt
    return mkdir_string(tgt);
}

int touch_file(const char *path){
    int ret;
    bool ok;

    // does file exist yet?
    ret = exists(path, &ok);
    if(ret) return ret;
    if(ok){
        // just update the timestamp
        ret = update_timestamp(path);
        if(ret) perror(path);
        return ret;
    }

    // possibly create parent directory
    string_t spath = { .text = path, .len = strlen(path) };
    ret = mkdirs(ddirname(spath));
    if(ret) return ret;

    // create file
    FILE *f = fopen(path, "w");
    if(!f){
        perror(path);
        return 1;
    }

    // check fclose since we were writing
    ret = fclose(f);
    if(ret){
        perror("fclose");
        return 1;
    }

    return 0;
}

int print_help(FILE *f){
    fprintf(f, "usage: stamp OUTPUT\n");
    // return 0 or 1 to make main easier to write.
    return f == stdout ? 0 : 1;
}


int print_version(void){
    fprintf(stdout, "%s\n", VERSION);
    // return 0 to make main easier to write.
    return 0;
}


int main(int argc, char **argv){
    // parse args
    char *output = NULL;
    bool nomoreflags = false;
    for(int i = 1; i < argc; i++){
        if(nomoreflags && output) return print_help(stderr);
        else if(nomoreflags) output = argv[i];
        else if(strcmp(argv[i], "--help") == 0) return print_help(stdout);
        else if(strcmp(argv[i], "-h") == 0) return print_help(stdout);
        else if(strcmp(argv[i], "--version") == 0) return print_version();
        else if(strcmp(argv[i], "--") == 0) nomoreflags = true;
        else if(output) return print_help(stderr);
        else output = argv[i];
    }
    if(!output) return print_help(stderr);

    return touch_file(output);
}
