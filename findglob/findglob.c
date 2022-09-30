#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <limits.h>
#include <sys/stat.h>

#ifndef _WIN32 // UNIX
    #include <dirent.h>
#else // WINDOWS
    #include <windows.h>
#endif

#define VERSION "0.1.2"

// F(string) matches a "%.*s" in a format string
#define F(string) (int)(string).len, (string).text

int print_help(FILE *f){
    return fprintf(f,
"findglob will find matching files and directories and write them to stdout.\n"
"\n"
"usage: findglob PATTERN... [ANTIPATERN...]\n"
"\n"
"examples:\n"
"\n"
"    # find all .c files below a directory\n"
"    findglob '**/*.c'\n"
"\n"
"    # find all .c AND .h files below a directory\n"
"    findglob '**/*.c' '**/*.h'\n"
"\n"
"    # find all .c AND .h files below a directory, while avoid searching\n"
"    # through the .git directory\n"
"    findglob '**/*.c' '**/*.h' '!.git'\n"
"\n"
"    # find all .py files below a directory, while avoid searching through\n"
"    # the git directory or any __pycache__ directories\n"
"    findglob '**/*.py' '!.git' '!**/__pycache__'\n"
"\n"
"    # find all .c files below a directory but ignore any .in.c files\n"
"    findglob '**/*.c' '!**/*.in.c'\n"
"\n"
"Some details of how patterns work:\n"
"\n"
"  - a PATTERN starting with ** will begin searching in $PWD\n"
"\n"
"  - a PATTERN starting with prefix/** will begin searching at prefix/\n"
"\n"
"  - PATTERNs of a/** and b/** will search a/ and b/ in sequence\n"
"\n"
"  - PATTERNs of **/a and **/b will search $PWD once for files named a or b,\n"
"    because they have the same start point ($PWD)\n"
"\n"
"  - PATTERNs of a/** and a/b/** will search a/ once, since the start point\n"
"    of the first pattern is a parent of the start point of the second\n"
"\n"
"  - PATTERNs ending with a file separator ('/') will only match directories\n"
"\n"
"  - ANTIPATTERNs start with a '!', and cause matching files to not be\n"
"    printed and matching directories to not be searched\n"
"\n"
"  - ANTIPATTERNs follow the same startpoint rules, so !**/.git will prevent\n"
"    matching anything beneath $PWD named .git, while !/**/.git, which has a\n"
"    start point of / will prevent matching anything named .git across the\n"
"    entire filesystem.  Unlike PATTERNs, an ANTIPATTERN with a start point\n"
"    of '/' is not enough to cause findglob to search through all of '/'.\n"
"\n"
"  - PATTERNs and ANTIPATTERNs may have types.  Presently only dir-types and\n"
"    file-types (really, non-dir-types) exist.  Dir-type patterns will match\n"
"    directories but not files, file-types will match files but not dirs,\n"
"    and untyped patterns will match either.  Dir-type patterns may be\n"
"    specified with a trailing file separator (/).  File-type patterns must\n"
"    be specified with the extended syntax.\n"
"\n"
"  - on Windows, using '\\' as a separator is not allowed; use '/' instead\n"
"\n"
"Extended syntax:\n"
"\n"
"  - Extended-syntax patterns begin with a ':', followed by zero or more\n"
"    flags, followed by another ':', followed by the pattern.  The following\n"
"    flags are currently supported:\n"
"\n"
"      - ! -> an ANTIPATTERN\n"
"      - f -> match against files\n"
"      - d -> match against directories\n"
"      - if no type flag is supplied, it matches all types\n"
"\n"
"   Example:\n"
"       # find files (not dirs) named 'build' except those in build dirs:\n"
"       findglob ':f:**/build' ':!d:**/build'\n"
    );
}

#ifdef _WIN32 // WINDOWS
#include <windows.h>
#include <fileapi.h>
#include <sys/utime.h>

void win_perror(const char *msg){
    char buf[256];
    DWORD winerr = GetLastError();
    winerr = FormatMessageA(
                FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                NULL,
                GetLastError(),
                MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                buf, (DWORD)sizeof(buf), NULL);
    buf[sizeof(buf) - 1] = '\0';
    fprintf(stderr, "%s: %s", msg, buf);
}

#define PATH_MAX MAX_PATH
#define S_ISDIR(mode) (((mode) & S_IFMT) == S_IFDIR)
#endif

struct pool_t;
typedef struct pool_t pool_t;

struct pool_t {
    size_t cap;
    size_t used;
    pool_t *last;
    char mem[1];
};

pool_t *pool_new(size_t n, pool_t *last){
    pool_t *p = malloc(n);
    if(!p){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    *p = (pool_t){
        .cap = n - (((uintptr_t)&p->mem[0]) - (uintptr_t)p),
        .last = last,
    };
    return p;
}

// assumes n is relatively short
void *xmalloc(pool_t **p, size_t n){
    if(*p == NULL || (*p)->cap - (*p)->used < n){
        // 2**20 bytes
        *p = pool_new(1048576, *p);
    }
    void *mem = (*p)->mem + (*p)->used;
    (*p)->used += n;
    return mem;
}

void pool_free(pool_t **p){
    while(*p){
        pool_t *last = (*p)->last;
        free(*p);
        *p = last;
    }
}

// string_t

typedef struct {
    size_t len;
    char *text;
} string_t;

string_t string_dup(pool_t **p, const char *c){
    size_t len = strlen(c);
    string_t string = {
        .len = len,
        .text = xmalloc(p, len+1),
    };
    memcpy(string.text, c, len+1);
    return string;
}

int string_cmp(const string_t a, const string_t b){
    size_t n = a.len < b.len ? a.len : b.len;
    int cmp = strncmp(a.text, b.text, n);
    if(cmp) return cmp;
    // n-length substrings match, return -1 if a is shorter, 1 if b is shorter
    if(a.len < b.len) return -1;
    return (int)(b.len < a.len);
}


bool string_eq(string_t a, string_t b){
    return a.len == b.len && strncmp(a.text, b.text, a.len) == 0;
}

int string_icmp(const string_t a, const string_t b){
    size_t n = a.len < b.len ? a.len : b.len;
#ifndef _WIN32 // UNIX
    int cmp = strncasecmp(a.text, b.text, n);
#else // WINDOWS
    int cmp = _strnicmp(a.text, b.text, n);
#endif
    if(cmp) return cmp;
    // n-length substrings match, return -1 if a is shorter, 1 if b is shorter
    if(a.len < b.len) return -1;
    return (int)(b.len > a.len);
}

bool string_ieq(string_t a, string_t b){
    return a.len == b.len &&
#ifndef _WIN32 // UNIX
        strncasecmp(a.text, b.text, a.len) == 0;
#else // WINDOWS
        _strnicmp(a.text, b.text, a.len) == 0;
#endif
}

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))

string_t string_sub(const string_t in, size_t start, size_t end){
    // decide start-offset
    size_t so = MIN(in.len, start);
    // decide end-offset
    size_t eo = MIN(in.len, end);

    // don't let eo < so
    eo = MAX(so, eo);

    return (string_t){ .text = in.text + so, .len = eo - so };
}

bool string_startswith(const string_t s, const string_t tgt){
    return s.len >= tgt.len && memcmp(s.text, tgt.text, tgt.len) == 0;
}

bool string_endswith(const string_t s, const string_t tgt){
    return s.len >= tgt.len && memcmp(
        s.text+(s.len-tgt.len), tgt.text, tgt.len
    ) == 0;
}

bool string_contains(const string_t s, const string_t tgt){
    for(size_t i = 0; i + tgt.len <= s.len; i++){
        if(memcmp(s.text+i, tgt.text, tgt.len) == 0) return true;
    }
    return false;
}

// code copied from splintermail project, then modified
// (https://sr.ht/~splintermail-dev/splintermail-client)
// original code was public domain under the UNLICENSE

static const string_t DOT = { .text = ".", .len = 1 };
static const string_t DOTDOT = { .text = "..", .len = 2 };

static bool _is_sep(char c){
    #ifndef _WIN32 // UNIX
    return c == '/';
    #else // WINDOWS
    return c == '/';
    // allowing backslash as a separator breaks our escape parsing
    // return c == '/' || c == '\\';
    #endif
}

// returns the number of leading seps at the end of path, excluding skip
static size_t _get_leading_sep(const string_t path, size_t skip){
    size_t out = 0;
    for(; out + skip < path.len; out++){
        if(!_is_sep(path.text[out + skip])) return out;
    }
    return out;
}

// returns the number of leading non-seps at the end of path, excluding skip
static size_t _get_leading_nonsep(const string_t path, size_t skip){
    size_t out = 0;
    for(; out + skip < path.len; out++){
        if(_is_sep(path.text[out + skip])) return out;
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

// end of splintermail code

// returns nonzero on error
int path_extend(string_t *base, const string_t text, size_t cap){
    bool needsep = (base->len && !_is_sep(base->text[base->len-1]));
    // length check
    if(base->len + (int)needsep + text.len + 1 > cap){
        // leave base unmodified
        return -1;
    }
    if(needsep) base->text[base->len++] = '/';
    memcpy(&base->text[base->len], text.text, text.len);
    base->len += text.len;
    base->text[base->len] = '\0';
    return 0;
}

// path_iter_t

typedef struct {
    string_t base;
    size_t nskip;
    bool ok;
    bool isvol;
    size_t i;
} path_iter_t;

string_t path_next(path_iter_t *it){
    it->isvol = false;
    if(!it->ok) return (string_t){0};
    // increment i once anytime we are iterating for a second time
    if(it->nskip) it->i++;
    if(it->nskip >= it->base.len) {
        it->ok = false;
        return (string_t){0};
    }

    if(it->nskip == 0){
        // check for a volume
        size_t nvolume = _get_volume(it->base);
        if(nvolume){
            // the nvolume becomes the first part of the path
            it->nskip = nvolume;
            it->isvol = true;
            return string_sub(it->base, 0, nvolume);
        }
    }

    // skip leading sep
    size_t nsep = _get_leading_sep(it->base, it->nskip);
    size_t nsect = _get_leading_nonsep(it->base, it->nskip + nsep);
    if(!nsect){
        // no more sections to read
        it->nskip = it->base.len;
        it->ok = false;
        return (string_t){0};
    }
    // new section
    size_t start = it->nskip + nsep;
    size_t end = start + nsect;
    it->nskip = end;
    return string_sub(it->base, start, end);
}

string_t path_iter(path_iter_t *it, string_t base){
    *it = (path_iter_t){ .base=base, .nskip = 0, .ok = true, .i = 0};
    return path_next(it);
}

// pattern_t, for paths

// optimization strategies
typedef enum {
    OPT_ANY,       // e.g. *
    OPT_PREFIX,    // e.g. test_*
    OPT_SUFFIX,    // e.g. *.c
    OPT_BOOKENDS,  // e.g. test_*.c
    OPT_CONTAINS,  // e.g. *1999*
    OPT_NONE,      // run the full match engine
} opt_e;

typedef struct {
    opt_e opt;
    string_t text;
    // only for OPT_BOOKENDS
    string_t text2;
    // flag each character as literal or not, only for OPT_NONE
    bool *lit;
} glob_t;

typedef enum {
    SECTION_ANY,       // "**"
    SECTION_CONSTANT,  // "asdf"
    SECTION_GLOB,      // "*.c"
} section_e;

typedef union {
    // nothing for SECTION_ANY
    string_t constant;
    glob_t glob;
} section_u;

typedef struct {
    section_e type;
    section_u val;
} section_t;

typedef enum {
    CLASS_FILE = 1,
    CLASS_DIR = 2,
    CLASS_ANY = 3,  // CLASS_FILE | CLASS_DIR
} class_e;

// a parsed pattern, split on file system separators
typedef struct {
    section_t *sects;
    size_t len;
    size_t cap;
    bool anti;
    class_e class;
    // start gets rewritten by realpath/GetFullPathNameA at some point
    // must be nul-terminated
    string_t start;
    // printstart is based on the originally provided start
    string_t printstart;
    // for our guaranteed-stable qsort
    size_t order;
} pattern_t;

static const string_t STAR = { .text = "*", .len = 1 };
static const string_t QUESTION = { .text = "?", .len = 1 };
static const string_t DOUBLESTAR = { .text = "**", .len = 2 };

int section_parse(section_t *sect, string_t s){
    *sect = (section_t){0};
    if(s.len == 0){
        // these should be filtered out by path_iter_t
        fprintf(stderr, "illegal empty section\n");
        return 1;
    }

    if(string_eq(s, DOUBLESTAR)){
        *sect = (section_t){ .type = SECTION_ANY };
        return 0;
    }

    char buf[PATH_MAX];
    bool lit[PATH_MAX];
    size_t len = 0;
    bool escaped = false;
    // for optimizations
    size_t nstar = 0;
    size_t star1 = 0;
    size_t star2 = 0;
    size_t nquestion = 0;
    for(size_t i = 0; i < s.len; i++){
        if(len == sizeof(buf)){
            fprintf(stderr, "section longer than PATH_LEN!\n");
            return 1;
        }
        char c = s.text[i];
        switch(c){
            case '\\':
                if(!escaped){
                    escaped = true;
                    continue;
                }
                buf[len] = '\\';
                lit[len++] = true;
                break;

            case '*':
                buf[len] = '*';
                lit[len] = escaped;
                if(!escaped){
                    nstar++;
                    if(nstar == 1) star1 = len;
                    if(nstar == 2) star2 = len;
                }
                // disallow duplicate '*'s
                if(!escaped && len && buf[len-1] == '*' && !lit[len-1]){
                    fprintf(stderr, "consecutive * wildcards not allowed\n");
                    fprintf(stderr, "note: x/** is legal but x** is not\n");
                    return 1;
                }
                len++;
                break;

            case '?':
                buf[len] = '?';
                lit[len++] = escaped;
                nquestion += !escaped;
                break;

            default:
                if(escaped){
                    fprintf(stderr, "illegal escape: \\%c\n", c);
                    fprintf(stderr, "legal escapes are: \\* \\? \\\\\n");
                    return 1;
                }
                buf[len] = c;
                lit[len++] = true;

        }
        escaped = false;
    }
    if(escaped){
        fprintf(stderr, "illegal trailing '\\'\n");
        return 1;
    }
    // the bare * case
    if(len == 1 && nstar == 1){
        *sect = (section_t){
            .type = SECTION_GLOB,
            .val = { .glob = { .opt = OPT_ANY } },
        };
        return 0;
    }

    char *out = malloc(len + 1);
    if(!out){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(out, buf, len);
    out[len] = '\0';

    if(nquestion == 0 && nstar == 0){
        // constant case
        *sect = (section_t){
            .type = SECTION_CONSTANT,
            .val = { .constant = { .text = out, .len = len } },
        };
        return 0;
    }

    if(nquestion == 0 && nstar == 1 && star1 == 0){
        // *abc: the suffix case
        *sect = (section_t){
            .type = SECTION_GLOB,
            .val = {
                .glob = {
                    .opt = OPT_SUFFIX,
                    .text = { .text = out+1, .len = len-1 },
                },
            },
        };
        return 0;
    }

    if(nquestion == 0 && nstar == 1 && star1 == len-1){
        // abc*: the prefix case
        *sect = (section_t){
            .type = SECTION_GLOB,
            .val = {
                .glob = {
                    .opt = OPT_PREFIX,
                    .text = { .text = out, .len = len-1 },
                },
            },
        };
        return 0;
    }

    if(nquestion == 0 && nstar == 1){
        // a*b: the bookends case
        *sect = (section_t){
            .type = SECTION_GLOB,
            .val = {
                .glob = {
                    .opt = OPT_BOOKENDS,
                    .text = { .text = out, .len = star1 },
                    .text2 = { .text = out+star1+1, .len = len-star1-1 },
                },
            },
        };
        return 0;
    }

    if(nquestion == 0 && nstar == 2 && star1 == 0 && star2 == len-1){
        // *abc*: the contains case
        *sect = (section_t){
            .type = SECTION_GLOB,
            .val = {
                .glob = {
                    .opt = OPT_CONTAINS,
                    .text = { .text = out+1, .len = len-2 },
                },
            },
        };
        return 0;
    }

    // anything else: run the full glob matching logic
    bool *litout = malloc(len);
    if(!out){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    memcpy(litout, lit, len);
    *sect = (section_t){
        .type = SECTION_GLOB,
        .val = {
            .glob = {
                .opt = OPT_NONE,
                .text = { .text = out, .len = len },
                .lit = litout,
            },
        },
    };
    return 0;
}

void section_free(section_t *sect){
    switch(sect->type){
        case SECTION_ANY:
            break;
        case SECTION_CONSTANT:
            free(sect->val.constant.text);
            break;
        case SECTION_GLOB:
            switch(sect->val.glob.opt){
                case OPT_ANY:
                    break;
                case OPT_NONE:
                    free(sect->val.glob.lit);
                    // fallthru
                case OPT_PREFIX:
                case OPT_BOOKENDS:
                    free(sect->val.glob.text.text);
                    break;
                case OPT_SUFFIX:
                case OPT_CONTAINS:
                    // deal with initial * that we skipped
                    free(&sect->val.glob.text.text[-1]);
                    break;
            }
            break;
    }
    *sect = (section_t){0};
}

void pattern_add_section(pattern_t *pattern, section_t sect){
    if(pattern->len == pattern->cap){
        pattern->cap = pattern->cap ? pattern->cap*2 : 32;
    }
    pattern->sects = realloc(
        pattern->sects, pattern->cap * sizeof(*pattern->sects)
    );
    if(!pattern->sects){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    pattern->sects[pattern->len++] = sect;
}

// returns length of consumed bytes, or 0 on error
size_t extended_syntax_parse(string_t path, bool *anti, class_e *class){
    *anti = false;
    *class = 0;
    for(size_t i = 1; i < path.len; i++){
        switch(path.text[i]){
            case ':':
                // no type flags implies all type flags
                if(!*class) *class = CLASS_ANY;
                return i+1;

            case '!':
                if(*anti){
                    fprintf(
                        stderr, "duplicate '!' in extended syntax pattern\n"
                    );
                    return 0;
                }
                *anti = true;
                break;

            case 'd':
                if(*class & CLASS_DIR){
                    fprintf(
                        stderr, "duplicate 'd' in extended syntax pattern\n"
                    );
                    return 0;
                }
                *class |= CLASS_DIR;
                break;

            case 'f':
                if(*class & CLASS_FILE){
                    fprintf(
                        stderr, "duplicate 'f' in extended syntax pattern\n"
                    );
                    return 0;
                }
                *class |= CLASS_FILE;
                break;

            default:
                fprintf(
                    stderr,
                    "unrecognized flag '%c' in extended syntax pattern\n",
                    path.text[i]
                );
                return 0;
        }
    }
    // if we are here, we ran out of text
    fprintf(
        stderr, "incomplete extended syntax pattern: missing closing ':'\n"
    );
    return 0;
}

int pattern_parse(pattern_t *pattern, char *text){
    *pattern = (pattern_t){0};
    string_t path = { .text = text, .len = strlen(text) };

    if(path.len == 0 || (path.len == 1 && path.text[0] == '!')){
        fprintf(stderr, "empty pattern not allowed\n");
        return 1;
    }

    bool isextended = (path.text[0] == ':');
    bool anti = false;
    class_e class = CLASS_ANY;
    if(isextended){
        // handle extended syntax patterns
        size_t ext = extended_syntax_parse(path, &anti, &class);
        if(!ext) return 1;
        path = string_sub(path, ext, path.len);
    }else{
        // handle shorthand antipattern notation (leading '!')
        if(path.text[0] == '!'){
            path = string_sub(path, 1, path.len);
            anti = true;
        }
        // handle shorthand dir-only notation (trailing '/')
        if(_is_sep(path.text[path.len-1])){
            class = CLASS_DIR;
        }
    }

    pattern->anti = anti;
    pattern->class = class;

    path_iter_t it;
    for(string_t sub = path_iter(&it, path); it.ok; sub = path_next(&it)){
        if(it.isvol){
            // volume case: wildcards not allowed
            // still need to malloc the backing memory so section_free() works
            char *subcopy = malloc(sub.len);
            if(!subcopy){
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
            memcpy(subcopy, sub.text, sub.len);
            section_t sect = {
                .type = SECTION_CONSTANT,
                .val = { .constant = { .text = subcopy, .len = sub.len } },
            };
            pattern_add_section(pattern, sect);
            continue;
        }
        // normal case
        section_t sect;
        int ret = section_parse(&sect, sub);
        if(ret) return ret;
        pattern_add_section(pattern, sect);
    }

    // check that there are no consecutive **'s
    bool was = false;
    for(size_t i = 0; i < pattern->len; i++){
        bool is = (pattern->sects[i].type == SECTION_ANY);
        if(was && is){
            fprintf(
                stderr,
                "a pattern cannot have two consecutive '**' elements\n"
            );
            return 1;
        }
        was = is;
    }

    // build the start
    pattern->start = (string_t){ .text = malloc(PATH_MAX), .len = 0 };
    if(!pattern->start.text){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    // null-terminate the empty-start case
    pattern->start.text[0] = '\0';
    for(size_t i = 0; i < pattern->len; i++){
        section_t sect = pattern->sects[i];
        if(sect.type != SECTION_CONSTANT) break;
        if(path_extend(&pattern->start, sect.val.constant, PATH_MAX)){
            fprintf(stderr, "pattern start is too long\n");
            return 1;
        }
    }

    // copy the start to the printstart
    pattern->printstart = (string_t){ .text = malloc(PATH_MAX), .len = 0 };
    if(!pattern->printstart.text){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    pattern->printstart.len = pattern->start.len;
    memcpy(
        pattern->printstart.text,
        pattern->start.text,
        pattern->start.len + 1
    );
    return 0;
}

/* replace the original start, including its CONSTANT sections, with a new one,
   for converting a pattern from relative to absolute.

   Note that the main reason to not just use realpath/GetFullPathNameA inside
   pattern_parse() is that calls to realpath() are hard to unit test, since all
   those files must actually exist.  This strategy lets us unit test the
   parsing and rewriting logic a lot easier.  Plus, we would still have to have
   some sort of two-pass strategy for finding the real start, since we would
   have to parse sections initially to know what to call realpath() on in the
   first place.  This is slightly less efficient but eaiser to test. */
int pattern_rewrite_start(pattern_t *pattern, string_t new){
    path_iter_t it;
    // count sections of the old start
    size_t nold = 0;
    for(path_iter(&it, pattern->start); it.ok; path_next(&it)){
        nold++;
    }
    // free the sections we're removing
    for(size_t i = 0; i < nold; i++){
        section_free(&pattern->sects[i]);
    }
    // count sections of the new start
    size_t nnew = 0;
    for(path_iter(&it, new); it.ok; path_next(&it)){
        nnew++;
    }
    // count sections which were not in start
    size_t nextra = pattern->len - nold;
    // append some dummy section_t's to allocate any space needed
    for(size_t i = nold; i < nnew; i++){
        pattern_add_section(pattern, (section_t){.type=SECTION_GLOB});
    }
    // fix the length
    pattern->len = nnew + nextra;
    // shift the section_t's we're keeping to the left or right
    if(nold != nnew){
        memmove(
            &pattern->sects[nnew],
            &pattern->sects[nold],
            nextra*sizeof(*pattern->sects)
        );
    }
    // add in new sections
    for(string_t sub = path_iter(&it, new); it.ok; sub = path_next(&it)){
        // wildcards not allowed in start, no need for section_parse
        // still need to malloc the backing memory so section_free() works
        char *subcopy = malloc(sub.len);
        if(!subcopy){
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
        memcpy(subcopy, sub.text, sub.len);
        section_t sect = {
            .type = SECTION_CONSTANT,
            .val = { .constant = { .text = subcopy, .len = sub.len } },
        };
        pattern->sects[it.i] = sect;
    }
    // replace pattern->start
    if(new.len + 1 > PATH_MAX){
        fprintf(stderr, "realpath(start) is too long\n");
        return 1;
    }
    pattern->start.len = new.len;
    memcpy(pattern->start.text, new.text, new.len);
    pattern->start.text[new.len] = '\0';
    return 0;
}

void pattern_free(pattern_t *pattern){
    for(size_t i = 0; i < pattern->len; i++){
        section_free(&pattern->sects[i]);
    }
    free(pattern->sects);
    free(pattern->start.text);
    free(pattern->printstart.text);
    *pattern = (pattern_t){0};
}

/* A "resuable array" is one where we need an array of unknown length of some
   type at each layer of recursion.  We use normal malloc() for this array, but
   after descending into one subdir, we'll reuse each array when we descend
   into the next subdir. */
/* A "resusable" array means there's an API for stacking allocated arrays after
   using them and reusing them from the stack when you need another.  We use
   this for array lists where we need one-per-directory-level, so we allocate
   O(MAXDEPTH) array lists rather than O(NUMDIRS). */
#define DEFINE_REUSABLE_ARRAY(NAME, TYPE) \
    struct NAME##_array_t; \
    typedef struct NAME##_array_t NAME##_array_t; \
    struct NAME##_array_t { \
        TYPE *items; \
        size_t len; \
        size_t cap; \
        NAME##_array_t *next; \
    }; \
    void NAME##_array_add(NAME##_array_t *arr, TYPE NAME){ \
        if(arr->len == arr->cap){ \
            arr->cap *= 2; \
            arr->items = realloc( \
                arr->items, arr->cap * sizeof(*arr->items) \
            ); \
            if(!arr->items){ \
                fprintf(stderr, "out of memory\n"); \
                exit(1); \
            } \
        } \
        arr->items[arr->len++] = NAME; \
    } \
    void NAME##_array_free(NAME##_array_t **mem){ \
        /* the *array_t itself needs no free, it's in the pool */ \
        for(NAME##_array_t *arr = *mem; arr; arr = arr->next){ \
            free(arr->items); \
        } \
    } \
    NAME##_array_t *NAME##_array_get( \
        pool_t **p, NAME##_array_t **mem, size_t basecap \
    ){ \
        if(*mem){ \
            /* reuse case */ \
            NAME##_array_t *out = *mem; \
            *mem = out->next; \
            out->next = NULL; \
            out->len = 0; \
            return out; \
        } \
        /* new case */ \
        NAME##_array_t *out = xmalloc(p, sizeof(*out)); \
        *out = (NAME##_array_t){ .cap = basecap }; \
        out->items = malloc(out->cap * sizeof(*out->items)); \
        if(!out->items){ \
            fprintf(stderr, "out of memory\n"); \
            exit(1); \
        } \
        return out; \
    } \
    void NAME##_array_put(NAME##_array_t **mem, NAME##_array_t *arr){ \
        arr->next = *mem; \
        *mem = arr; \
    }

// match_t

// the state of a single pattern we are traversing
typedef struct {
    const pattern_t *pattern;
    size_t matched;
} match_t;

// we'll need an array of match state at every directory level.
DEFINE_REUSABLE_ARRAY(match, match_t);

typedef enum {
    MATCH_NONE = 0,
    // MATCH_0 means "remove no elements and reuse the pattern"
    MATCH_0 = 1,
    // MATCH_1 means "remove one element and reuse the pattern"
    MATCH_1 = 2,
    // MATCH_2 means "remove two elements and reuse the pattern"
    MATCH_2 = 4,
    // MATCH_TERMINAL replaces either 1 or 2 when it completes the pattern
    MATCH_TERMINAL = 8,
} match_flags_e;

bool glob_match(string_t glob, bool *lit, string_t text){
    if(!glob.len) return text.len == 0;
    if(!text.len) return string_eq(glob, STAR) && !lit[0];
    char *g = glob.text;
    size_t ig = 0;
    bool *l = lit;
    char *t = text.text;
    size_t it = 0;
    while(true){
        // literal matches, or a '?' wildcard
        if(*l || *g == '?'){
            if(*g != *t && *l)
                return false;
            // consume g/l and t
            g++; l++; ig++; t++; it++;
            if(ig == glob.len){
                // end condition
                return it == text.len;
            }
            // ig != glob.len
            if(it == text.len){
                // allow a single remaining unmatched *
                return ig+1 == glob.len && *g == '*' && !*l;
            }
            continue;
        }
        // otherwise, *g == '*':
        if(ig+1 == glob.len)
            // special case: return immediately on the final *
            return true;
        // attempt to match without this '*' at all
        if(glob_match(
            (string_t){ .text = g+1, .len = glob.len - ig - 1 },
            l+1,
            (string_t){ .text = t, .len = text.len - it }
        ))
            return true;
        // otherwise consume t and try again
        t++; it++;
        if(it == text.len)
            return false;
    }
}

bool section_matches(section_t sect, string_t text){
    switch(sect.type){
        case SECTION_CONSTANT: return string_eq(sect.val.constant, text);
        case SECTION_ANY: return true;
        case SECTION_GLOB: break; // fallthru
        default:
            fprintf(stderr, "unrecognized section_e: %d\n", sect.type);
            exit(1);
    }
    // SECTION_GLOB
    switch(sect.val.glob.opt){
        case OPT_ANY:
            return true;

        case OPT_PREFIX:
            return string_startswith(text, sect.val.glob.text);

        case OPT_SUFFIX:
            return string_endswith(text, sect.val.glob.text);

        case OPT_CONTAINS:
            return string_contains(text, sect.val.glob.text);

        case OPT_BOOKENDS:
            return (
                text.len >= sect.val.glob.text.len + sect.val.glob.text2.len
                && string_startswith(text, sect.val.glob.text)
                && string_endswith(text, sect.val.glob.text2)
            );

        case OPT_NONE:
            return glob_match(sect.val.glob.text, sect.val.glob.lit, text);

        default:
            fprintf(stderr, "unrecognized opt_e: %d\n", sect.val.glob.opt);
            exit(1);
    }
}

// match_text() by example:
//
// pattern | text | flags | next pattern(s)
// --------|------|---------------------------------
// b/c/a   |  a   | NONE  | -
// a/b/c   |  a   | 1     | b/c
// **/b/c  |  a   | 0     | **/b/c
// **/a/b  |  a   | 0,2   | **/a/b, b
// **/a    |  a   | 0,T   | **/a
// **/a/** |  a   | 2,T   | **
// a       |  a   | T     |
// a/**    |  a   | 1,T   | **
// a/**/b  |  a   | 1     | **/b
// **      |  a   | 0,T   | **
//
// Note that certain resulting patterns are omitted.  Even if you technically
// can match 0,1,2 (such as the **/a/b case) at least one of those patterns
// would logically contain another another, since ** can be empty.
//
// Since our strategy is based the next patterns and the length remaining,
// the core combinations are:
//
// pattern   | text | isdir | flags | notes
// ----------|------|-------|-------|--------------------------
// x         |  a   |       | NONE  |
// a         |  a   |       | T[1]  |
// a/x       |  a   |       | 1     |
// a/**      |  a   |       | 1,T[2]|
// a/**/x    |  a   |       | 1     | no MATCH_2 since **/x also matches x
// **        |  a   |       | 0,T[1]|
// **/a      |  a   |       | 0,T[1]| no MATCH_1 since **/a also matches a
// **/a/**   |  a   |       | 2,T[2]| no MATCH_0 since ** also matches a
// **/a/**/x |  a   |       | 2     | no MATCH_0 since ** also matches a
// **/a/x    |  a   |       | 0,2   | no MATCH_1 since **/a/x also matches a/x
// **/x      |  a   |       | 0     | no MATCH_1 since **/x also matches x
//
// [1]: is terminal if the pattern class matches the class for the input text
// [2]: is terminal if classes match AND it is a directory
//
match_flags_e match_text(match_t match, string_t text, class_e class){
    // get the section we're interested in
    section_t section = match.pattern->sects[match.matched];
    bool classmatch = class & match.pattern->class;
    bool isdir = class == CLASS_DIR;
    size_t remains = match.pattern->len - match.matched;
    // x case
    if(!section_matches(section, text)) return MATCH_NONE;
    if(section.type == SECTION_ANY){
        if(remains == 1){
            // ** case
            // TERMINAL if class matches
            return MATCH_0 | (MATCH_TERMINAL*classmatch);
        }
        section_t next = match.pattern->sects[match.matched+1];
        if(!section_matches(next, text)){
            // **/x case
            return MATCH_0;
        }
        if(remains == 2){
            // **/a case
            // TERMINAL if class matches
            return MATCH_0 | (MATCH_TERMINAL*classmatch);
        }
        // remains must be > 2
        section_t nextnext = match.pattern->sects[match.matched+2];
        if(nextnext.type == SECTION_ANY){
            if(remains == 3){
                // **/a/** case
                // TERMINAL if classmatch && class is CLASS_DIR
                return MATCH_2 | (MATCH_TERMINAL*classmatch*isdir);
            }
            // **/a/**/x case
            return MATCH_2;
        }
        // **/a/x case
        return MATCH_0 | MATCH_2;
    }
    if(remains == 1){
        // a case
        // TERMINAL if class matches
        return MATCH_TERMINAL*classmatch;
    }
    if(remains == 2){
        section_t next = match.pattern->sects[match.matched+1];
        if(next.type == SECTION_ANY){
            // a/** case
            // TERMINAL if classmatch && class is CLASS_DIR
            return MATCH_1 | (MATCH_TERMINAL*classmatch*isdir);
        }
    }
    // a/x, a/**/x
    return MATCH_1;
}

match_t match_minus(match_t match, size_t n){
    if(match.matched + n >= match.pattern->len){
        fprintf(stderr, "array overflow in match_minus(%zu)\n", n);
        exit(1);
    }
    return (match_t){ .pattern = match.pattern, .matched = match.matched + n };
}

typedef struct {
    string_t name;
    bool isdir;
} file_t;

// we'll need an array of files at every directory level.
DEFINE_REUSABLE_ARRAY(file, file_t);

int _qsort_files_cmp(const void *aptr, const void *bptr){
    const file_t *a = aptr;
    const file_t *b = bptr;
    return string_cmp(a->name, b->name);
}

void qsort_files(file_array_t *a){
    qsort(a->items, a->len, sizeof(*a->items), _qsort_files_cmp);
}

// path_startswith is aware that "a/b/c" starts with "a/b" but "a/bb" does not
bool path_startswith(const string_t a, const string_t b){
    if(!string_startswith(a, b)) return false;
    // since a startswith b, a.len => b.len
    return
        (b.len && _is_sep(b.text[b.len-1]))  // b is a bare volume, ends in sep
        || a.len == b.len                    // a == b
        || _is_sep(a.text[b.len]);           // b is parent of a
}

#define MAX_PATTERNS 256
typedef struct {
    const pattern_t *patterns;
    size_t npatterns;
    size_t members[MAX_PATTERNS];
    size_t nmembers;
    size_t i;
} roots_iter_t;

// returns false when it finishes
bool roots_next(roots_iter_t *it){
    for(size_t i = it->i; i < it->npatterns; i++){
        // antipatterns are never roots
        if(it->patterns[i].anti) continue;
        const string_t a = it->patterns[i].start;
        // a is always the first member of the group
        it->members[0] = i;
        it->nmembers = 1;
        // isroot signifies that no other pattern would contain this pattern
        bool isroot = true;
        for(size_t j = 0; j < it->npatterns; j++){
            // skip self-comparisons
            if(i == j) continue;
            const string_t b = it->patterns[j].start;
            // all antipatterns are always included in each search
            if(it->patterns[j].anti){
                it->members[it->nmembers++] = j;
                continue;
            }
            /* first check if a is still possibly a root.
               Antipatterns can't be roots.
               If a startswith b then a == b or b is a parent of a.
               If a == b, we call the first one the root. */
            if(path_startswith(a, b) && (a.len != b.len || i > j)){
                isroot = false;
                break;
            }
            // otherwise check if b is a child of a
            if(path_startswith(b, a)){
                it->members[it->nmembers++] = j;
            }
        }
        if(!isroot) continue;
        // create a start from these patterns
        it->i = i+1;
        return true;
    }
    return false;
}

bool roots_iter(
    roots_iter_t *it,
    const pattern_t *patterns,
    size_t npatterns
){
    *it = (roots_iter_t){
        .patterns = patterns,
        .npatterns = npatterns,
        .i = 0,
    };

    return roots_next(it);
}

// shared memory across findglob recursion
typedef struct {
    const pattern_t *patterns;
    const size_t npatterns;
    pool_t *p;
    file_array_t *fa;
    match_array_t *ma;
    char *path;
    size_t len;
    size_t cap;
} mem_t;

void mem_free(mem_t *m){
    file_array_free(&m->fa);
    match_array_free(&m->ma);
    pool_free(&m->p);
    free(m->path);
    m->path = NULL;
}

bool keep_dir(const match_array_t *matches, string_t name){
    // always ignore "." or ".."
    if(string_eq(name, DOT)) return false;
    if(string_eq(name, DOTDOT)) return false;
    // filter directories which are obvious non-matches
    for(size_t i = 0; i < matches->len; i++){
        match_t match = matches->items[i];
        section_t section = match.pattern->sects[match.matched];
        if(match.pattern->anti) continue;
        if(!section_matches(section, name)) continue;
        return true;
    }
    return false;
}

bool keep_file(const match_array_t *matches, string_t name){
    // filter regular files which are not TERMINAL matches
    for(size_t i = 0; i < matches->len; i++){
        match_t match = matches->items[i];
        match_flags_e flags = match_text(match, name, CLASS_FILE);
        if(!(flags & MATCH_TERMINAL)) continue;
        return !match.pattern->anti;
    }
    return false;
}

void process_dir(
    const string_t name,
    const match_array_t *parent_matches,
    match_array_t *newmatches,
    bool *isintermediate,
    bool *isterminal
){
    *isterminal = false;
    *isintermediate = false;
    for(size_t i = 0; i < parent_matches->len; i++){
        match_t match = parent_matches->items[i];
        bool anti = match.pattern->anti;
        match_flags_e flags = match_text(match, name, CLASS_DIR);
        if(flags & MATCH_TERMINAL){
            // terminal antipatterns means we stop trying to match anything
            if(anti) return;
            *isterminal = true;
        }
        if(flags & MATCH_0){
            match_array_add(newmatches, match_minus(match, 0));
            if(!anti) *isintermediate = true;
        }
        if(flags & MATCH_1){
            match_array_add(newmatches, match_minus(match, 1));
            if(!anti) *isintermediate = true;
        }
        if(flags & MATCH_2){
            match_array_add(newmatches, match_minus(match, 2));
            if(!anti) *isintermediate = true;
        }
    }
}

// walk through components of start and create the starting set of match_t's
match_array_t *matches_init(
    pool_t **p,
    match_array_t **mem,
    const pattern_t *patterns,
    size_t npatterns,
    string_t start,
    string_t printstart,
    bool *isterminal
){
    *isterminal = false;
    // populate initial matches
    match_array_t *matches = match_array_get(p, mem, 32);
    for(size_t i = 0; i < npatterns; i++){
        match_t match = { .pattern = &patterns[i], .matched = 0 };
        match_array_add(matches, match);
    }

    // traverse through these match patterns, one section of start at a time
    path_iter_t it;
    bool _isterminal = false;
    for(string_t text = path_iter(&it, start); it.ok; text = path_next(&it)){
        match_array_t *newmatches = match_array_get(p, mem, 32);
        // we only care about the final isterminal
        _isterminal = false;
        bool isintermediate = false;
        process_dir(
            text, matches, newmatches, &isintermediate, &_isterminal
        );
        // non-intermediate means we don't continue
        if(!isintermediate){
            // if this was a perfect match, print before exiting
            if(_isterminal && (path_next(&it), !it.ok)){
                fprintf(
                    stdout, "%.*s\n", (int)printstart.len, printstart.text
                );
            }
            match_array_put(mem, matches);
            match_array_put(mem, newmatches);
            return match_array_get(p, mem, 32);;
        }
        match_array_put(mem, matches);
        matches = newmatches;
    }
    *isterminal = _isterminal;
    return matches;
}

// check if a file-type start should be included
bool matches_initial_file(
    pool_t **p,
    match_array_t **mem,
    const pattern_t *patterns,
    size_t npatterns,
    string_t start
){
    // populate initial matches
    match_array_t *matches = match_array_get(p, mem, 32);
    for(size_t i = 0; i < npatterns; i++){
        match_t match = { .pattern = &patterns[i], .matched = 0 };
        match_array_add(matches, match);
    }

    // count how many sections are in this start
    path_iter_t it;
    size_t last_sect = 0;
    for(path_iter(&it, start); it.ok; path_next(&it)) last_sect = it.i;

    // walk through the different sections of the start
    for(string_t text = path_iter(&it, start); it.ok; text = path_next(&it)){
        // the last section is not a directory
        if(it.i == last_sect){
            // last section is a file type
            bool keep = keep_file(matches, text);
            match_array_put(mem, matches);
            return keep;
        }

        match_array_t *newmatches = match_array_get(p, mem, 32);
        // we only care about the final isterminal
        bool isterminal = false;
        bool isintermediate = false;
        process_dir(
            text, matches, newmatches, &isintermediate, &isterminal
        );
        // non-intermediate means we don't continue
        if(!isintermediate){
            match_array_put(mem, matches);
            match_array_put(mem, newmatches);
            return false;
        }
        match_array_put(mem, matches);
        matches = newmatches;
    }

    // we should have already exited
    fprintf(stderr, "matches_initial_file did not exit properly\n");
    exit(1);
}

// recursive layer beneath findglob
int _findglob(
    mem_t *m,
    char **path,
    size_t *pathcap,
    size_t pathlen,
    const match_array_t *parent_matches
){
    int retval = 0;
    file_array_t *files = file_array_get(&m->p, &m->fa, 1024);

    size_t maxlen = 0;

#ifndef _WIN32 // UNIX

    // empty-start case: open '.' instead
    char *openpath = pathlen ? *path : ".";
    DIR *d = opendir(openpath);
    if(!d){
        perror(openpath);
        if(errno == ENOMEM){
            exit(1);
        }
        retval = 1;
        goto cleanup;
    }

    struct dirent *entry;
    while((entry = readdir(d))){
        bool isdir = (entry->d_type == DT_DIR);
        string_t name = string_dup(&m->p, entry->d_name);

#else // WINDOWS

    // borrow our path buffer to write a search string for FindFirstFile
    size_t old_pathlen = pathlen;
    if(pathlen + 3 > *pathcap){
        *pathcap *= 2;
        *path = realloc(*path, *pathcap);
        if(!*path){
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
    }

    // handle volumes, which don't need an extra separator
    // add the joining '/' to non-volume paths
    if(pathlen && !_is_sep((*path)[pathlen-1])){
        (*path)[pathlen++] = '/';
    }

    (*path)[pathlen++] = '*';
    (*path)[pathlen++] = '\0';

    WIN32_FIND_DATA ffd;

    // use FindExInfoBasic since it is faster and sufficient for our needs
    HANDLE h = FindFirstFileEx(
        *path,
        FindExInfoBasic,
        &ffd,
        FindExSearchNameMatch,
        NULL,
        FIND_FIRST_EX_CASE_SENSITIVE
    );

    if(h == INVALID_HANDLE_VALUE){
        win_perror(*path);
        retval = 1;
        goto cleanup;
    }

    do{
        bool isdir = (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY);
        string_t name = string_dup(&m->p, ffd.cFileName);
#endif
        if(isdir){
            if(!keep_dir(parent_matches, name)) continue;
        }else{
            if(!keep_file(parent_matches, name)) continue;
        }
        file_t file = {
            .name = name,
            .isdir = isdir,
        };
        file_array_add(files, file);
        if(name.len > maxlen) maxlen = name.len;
#ifndef _WIN32 // UNIX
    }
    closedir(d);
#else // WINDOWS
    } while(FindNextFile(h, &ffd) != 0);
    FindClose(h);
    pathlen = old_pathlen;
#endif

    // sort for deterministic output
    qsort_files(files);

    // ensure that our path buffer is long enough for all files we kept
    // (existing len) + (max name len) + (1 for /) + (1 for \0)
    if(pathlen + maxlen + 2 > *pathcap){
        *pathcap *= 2;
        *path = realloc(*path, *pathcap);
        if(!*path){
            fprintf(stderr, "out of memory\n");
            exit(1);
        }
    }

    // handle volumes, which don't need an extra separator
    // add the joining '/' to non-volume paths
    if(pathlen && !_is_sep((*path)[pathlen-1])){
        (*path)[pathlen++] = '/';
    }

    for(size_t i = 0; i < files->len; i++){
        file_t file = files->items[i];
        memcpy(*path + pathlen, file.name.text, file.name.len);
        size_t sublen = pathlen + file.name.len;
        (*path)[sublen] = '\0';
        if(!file.isdir){
            // regular files: already known to be TERMINAL, just print
            fprintf(stdout, "%s\n", *path);
            continue;
        }
        // directories: print when terminal, recurse when intermediate
        bool isintermediate, isterminal;
        match_array_t *newmatches = match_array_get(&m->p, &m->ma, 32);
        process_dir(
            file.name, parent_matches, newmatches, &isintermediate, &isterminal
        );
        if(isterminal){
            fprintf(stdout, "%s\n", *path);
        }
        if(isintermediate){
            int ret = _findglob(m, path, pathcap, sublen, newmatches);
            // finish the loop but remember the error
            if(ret) retval = ret;
        }
        match_array_put(&m->ma, newmatches);
    }

cleanup:
    file_array_put(&m->fa, files);
    return retval;
}

int _qsort_pattern_cmp(const void *aptr, const void *bptr){
    const pattern_t *a = aptr;
    const pattern_t *b = bptr;
    if(a->anti != b->anti){
        // return 1 if b is anti
        return 2*(int)b->anti - 1;
    }
    // return 1 if b came after
    return 1 - 2*(int)(a->order < b->order);
}

void qsort_patterns(pattern_t *patterns, size_t npatterns){
    // guarantee a stable sort by assigning an order value to each pattern
    for(size_t i = 0; i < npatterns; i++){
        patterns[i].order = i;
    }
    qsort(patterns, npatterns, sizeof(*patterns), _qsort_pattern_cmp);
}


int findglob(
    pattern_t *patterns,
    size_t npatterns
){
    mem_t m = {
        .patterns = patterns,
        .npatterns = npatterns,
        .p = NULL,
        .fa = NULL,
        .ma = NULL,
    };
    // we reuse one path buffer for the entire recursion
    size_t pathcap = PATH_MAX;
    char *path = malloc(pathcap);
    if(!path){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    // we need a temp array of patterns
    pattern_t *temp_patterns = malloc(npatterns * sizeof(*patterns));
    if(!temp_patterns){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }

    // do a separate search for every root path we see
    int retval = 0;
    roots_iter_t it;
    for(
        bool ok = roots_iter(&it, patterns, npatterns);
        ok;
        ok = roots_next(&it)
    ){
        // copy group members into our temp buffer
        for(size_t i = 0; i < it.nmembers; i++){
            temp_patterns[i] = patterns[it.members[i]];
        }
        // capture start and printstart from the root
        string_t start = temp_patterns[0].start;
        string_t printstart = temp_patterns[0].printstart;
        // rearrange temp_patterns to have antipatterns first
        qsort_patterns(temp_patterns, it.nmembers);

        size_t minpathcap = MAX(printstart.len, start.len) + 1;
        if(minpathcap > pathcap){
            while(minpathcap > pathcap){
                pathcap *= 2;
            }
            path = realloc(path, pathcap);
            if(!path){
                fprintf(stderr, "out of memory\n");
                exit(1);
            }
        }

        // check if start points to a file or a directory
        memcpy(path, start.text, start.len);
        path[start.len] = '\0';
        struct stat st;
        int ret = stat(path, &st);
        if(ret){
            perror(path);
            exit(1);
        }

        if(!S_ISDIR(st.st_mode)){
            // special case: this start is a file
            if(
                matches_initial_file(
                    &m.p, &m.ma, temp_patterns, it.nmembers, start
                )
            ){
                fprintf(
                    stdout, "%.*s\n", (int)printstart.len, printstart.text
                );
            }
            continue;
        }

        memcpy(path, printstart.text, printstart.len);
        path[printstart.len] = '\0';
        bool isterminal;
        match_array_t *matches = matches_init(
            &m.p,
            &m.ma,
            temp_patterns,
            it.nmembers,
            start,
            printstart,
            &isterminal
        );
        if(isterminal){
            // empty-start case: print '.' instead
            fprintf(stdout, "%s\n", printstart.len ? path : ".");
        }
        if(matches->len){
            ret = _findglob(&m, &path, &pathcap, printstart.len, matches);
            // finish the loop but remember the error
            if(ret) retval = ret;
        }
        match_array_put(&m.ma, matches);
    }

    free(temp_patterns);
    free(path);
    mem_free(&m);
    return retval;
}

int findglob_main(int argc, char **argv){
    if(argc < 2){
        fprintf(stderr, "usage:   findglob PATTERN... [ANTIPATERN...]\n");
        fprintf(
            stderr, "example: findglob '**/*.c' '**/*.h' '!.git' '!tests'\n"
        );
        fprintf(stderr, "also try findglob --help\n");
        return 1;
    }
    if(strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0){
        print_help(stdout);
        return 0;
    }
    if(strcmp(argv[1], "--version") == 0){
        fprintf(stdout, "%s\n", VERSION);
        return 0;
    }
    int retval = 0;

    pattern_t *patterns = malloc((argc-1)*sizeof(*patterns));
    if(!patterns){
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    size_t npatterns = 0;
    size_t nantipatterns = 0;

    for(int i = 1; i < argc; i++){
        retval = pattern_parse(&patterns[npatterns++], argv[i]);
        if(retval) goto cleanup;
        if(patterns[npatterns-1].anti) nantipatterns++;
    }
    if(npatterns == nantipatterns){
        fprintf(
            stderr,
            "error: you provided %zu antipatterns but no patterns at all\n",
            nantipatterns
        );
        retval = 1;
        goto cleanup;
    }

    // rewrite all startpoints as absolute paths
    for(size_t i = 0; i < npatterns; i++){
        char buf[PATH_MAX];
        // handle the empty-start case
        char *oldname = patterns[i].start.len ? patterns[i].start.text : ".";
#ifndef _WIN32 // UNIX
        char *cret = realpath(oldname, buf);
        if(!cret){
            perror(oldname);
            retval = 1;
            goto cleanup;
        }
        // it's not 100% clear to me that realpath() guarnatees nul-termination
        string_t real = { .text = buf, .len = strnlen(buf, sizeof(buf)) };
#else // WINDOWS
        DWORD dret = GetFullPathNameA(oldname, sizeof(buf), buf, NULL);
        if(dret > sizeof(buf)){
            fprintf(stderr, "full path name is too long: %s\n", oldname);
            retval = 1;
            goto cleanup;
        }else if(dret == 0){
            win_perror(oldname);
            retval = 1;
            goto cleanup;
        }
        // use forwardslashes in path
        for(DWORD j = 0; j < dret; j++){
            if(buf[j] == '\\') buf[j] = '/';
        }
        string_t real = { .text = buf, .len = dret };
#endif
        retval = pattern_rewrite_start(&patterns[i], real);
        if(retval) goto cleanup;
    }

    retval = findglob(patterns, npatterns);

cleanup:
    for(size_t i = 0; i < npatterns; i++){
        pattern_free(&patterns[i]);
    }
    free(patterns);

    return retval;
}
