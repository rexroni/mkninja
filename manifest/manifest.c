#define WINDOWS_STAT_TIMESPEC 1
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <errno.h>

#define VERSION "0.1.2"

#define FILE_NOT_FOUND 2
#define STRING_NOT_FOUND ((size_t)-1)

#ifdef _WIN32 // WINDOWS
#include <windows.h>
#include <fileapi.h>
#include <sys/utime.h>

#define fopen fopen_compat
FILE *fopen_compat(const char *filename, const char *mode){
    FILE *f = NULL;
    errno_t x = fopen_s(&f, filename, mode);
    // the global errno is also set, so we can discard this.
    (void)x;
    return f;
}

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

void compat_perror(const char *msg){
    if(errno){
        perror(msg);
    }else{
        win_perror(msg);
    }
}

typedef FILETIME filetime_t;

int get_filetime(const char *path, filetime_t *out){
    *out = (filetime_t){0};

    HANDLE hfile = CreateFileA(
        // filename
        path,
        // access mode
        GENERIC_READ,
        // share mode
        FILE_SHARE_DELETE|FILE_SHARE_READ|FILE_SHARE_WRITE,
        // security attributes
        NULL,
        // creation disposition
        OPEN_EXISTING,
        // flags and attributes
        0,
        // template file (for setting attributes when creating a file)
        NULL
    );
    if(hfile == INVALID_HANDLE_VALUE){
        if(GetLastError() == ERROR_FILE_NOT_FOUND){
            return FILE_NOT_FOUND;
        }
        return 1;
    }

    BOOL ok = GetFileTime(hfile, NULL, NULL, out);
    if(!ok){
        win_perror(path);
    }
    CloseHandle(hfile);
    return !ok;
}

int compat_utime(const char *path){
    return _utime(path, NULL);
}

// result is true when a is newer than b
bool isnewer(filetime_t a, filetime_t b){
    return (
        a.dwHighDateTime > b.dwHighDateTime
        || (
            a.dwHighDateTime == b.dwHighDateTime
            && a.dwLowDateTime > b.dwLowDateTime
        )
    );
}

#else // UNIX
#include <utime.h>

#define compat_perror perror

typedef struct timespec filetime_t;

int get_filetime(const char *path, filetime_t *out){
    struct stat s = {0};
    int ret = stat(path, &s);
#ifdef __APPLE__
    *out = s.st_mtimespec;
#else
    *out = s.st_mtim;
#endif
    if(ret && errno == ENOENT) return FILE_NOT_FOUND;
    return ret != 0;
}

// result is true when a is newer than b
bool isnewer(filetime_t a, filetime_t b){
    return (
        a.tv_sec > b.tv_sec || (a.tv_sec == b.tv_sec && a.tv_nsec > b.tv_nsec)
    );
}

int compat_utime(const char *path){
    return utime(path, NULL);
}

#endif

typedef struct {
    size_t len;
    char *text;
} string_t;

int string_list_grow(string_t **mem, size_t *cap, size_t len){
    if(len < *cap) return 0;
    *cap *= 2;
    string_t *temp = realloc(*mem, *cap * sizeof(*temp));
    if(!temp){
        perror("realloc");
        return 1;
    }
    *mem = temp;
    return 0;
}

int cmp_string(const void *aptr, const void *bptr){
    const string_t *a = aptr;
    const string_t *b = bptr;
    size_t n = a->len < b->len ? a->len : b->len;
    int cmp = strncmp(a->text, b->text, n);
    if(cmp) return cmp;
    // n-length substrings match, return -1 if a is shorter, 1 if b is shorter
    if(a->len < b->len) return -1;
    return (int)(b->len < a->len);
}

bool string_eq(string_t a, string_t b){
    return a.len == b.len && strncmp(a.text, b.text, a.len) == 0;
}

// detects any of the following line endings: \r, \r\n, \n, \n\r
// the return value will have .text=buffer
string_t detect_endings(string_t text, char buffer[3]){
    size_t len = 0;
    for(size_t i = 0; i < text.len; i++){
        char c = text.text[i];
        if(c == '\r' || c == '\n'){
            if(!len){
                // first \r or \n
                buffer[len++] = c;
            }else if(buffer[0] == c){
                // \r\r or \n\n
                break;
            }else{
                // mixed case, \r\n or \n\r
                buffer[len++] = c;
                break;
            }
        }else if(len){
            // single-character case, \r or \n
            break;
        }
    }
    if(len){
        buffer[len] = '\0';
    }else{
        // we read the whole string and found no line endings
        buffer[len++] = '\0';
    }
    return (string_t){ .text=buffer, .len=len };
}


int read_stream(FILE *f, string_t *out){
    int retval = 0;
    char *buffer = NULL;
    *out = (string_t){0};

    size_t len = 0;
    size_t cap = 1024;
    buffer = malloc(cap);
    if(!buffer){
        perror("malloc");
        retval = 1;
        goto cu;
    }

    int c;
    // read the whole file, one character at a time
    while((c = fgetc(f)) != EOF){
        if(len+1 == cap){
            // double the buffer size
            size_t new_cap = cap * 2;
            char *new_buffer = realloc(buffer, new_cap);
            if(!new_buffer){
                perror("realloc");
                retval = 1;
                goto cu;
            }
            buffer = new_buffer;
            cap = new_cap;
        }
        // store the character
        buffer[len++] = (char)c;
    }
    // check for read error
    if(ferror(f)){
        perror("fgetc");
        retval = 1;
        goto cu;
    }
    // nul-terminate
    buffer[len] = '\0';
    *out = (string_t){.text=buffer, .len=len};

cu:
    if(retval) free(buffer);
    return retval;
}


int read_file(const char *path, string_t *out){
    *out = (string_t){0};
    FILE *f = fopen(path, "r");
    if(!f){
        perror(path);
        return 1;
    }
    int retval = read_stream(f, out);
    fclose(f);
    return retval;
}


int write_file(const char *path, string_t text){
    FILE *f = fopen(path, "w");
    if(!f){
        perror(path);
        return 1;
    }

    // write every character into the file
    for(size_t i = 0; i < text.len; i++){
        int ret = fputc(text.text[i], f);
        if(ret == EOF){
            perror("fputc");
            fclose(f);
            return 1;
        }
    }

    // check fclose since we were writing
    int ret = fclose(f);
    if(ret){
        perror("fclose");
        return 1;
    }

    return 0;
}


size_t find(string_t text, size_t off, string_t pat){
    if(pat.len > text.len) return STRING_NOT_FOUND;
    for(size_t i = off; i <= text.len - pat.len; i++){
        bool found = true;
        for(size_t j = 0; j < pat.len; j++){
            if(text.text[i+j] != pat.text[j]){
                found = false;
                break;
            }
        }
        if(found) return i;
    }
    return STRING_NOT_FOUND;
}

// this will mangle the input text
int split(string_t text, string_t sep, string_t **out, size_t *count_out){
    *out = NULL;
    *count_out = 0;
    string_t *strings = NULL;
    int retval = 0;

    size_t count = 0;
    size_t cap = 1024;
    strings = malloc(cap * sizeof(*strings));
    if(!strings){
        perror("malloc");
        retval = 1;
        goto cu;
    }

    size_t start = 0;
    size_t loc;
    while((loc = find(text, start, sep)) != STRING_NOT_FOUND){
        if(start == loc){
            // empty string, ignore
            start = loc + sep.len;
            continue;
        }
        retval = string_list_grow(&strings, &cap, count+1);
        if(retval) goto cu;
        strings[count++] = (string_t){
            .len=loc-start, .text=&text.text[start],
        };
        text.text[loc] = '\0';
        // start again after the separator
        start = loc + sep.len;
    }
    // trailing string?
    if(start < text.len){
        retval = string_list_grow(&strings, &cap, count+1);
        if(retval) goto cu;
        strings[count++] = (string_t){
            .len=text.len-start, .text=&text.text[start],
        };
    }

    *out = strings;
    *count_out = count;

cu:
    if(retval){
        // the contents of the strings do not need freeing
        free(strings);
    }
    return retval;
}


int join_names(const string_t *names, size_t n, string_t *out){
    // build a big list of all names, one per line
    *out = (string_t){0};
    size_t outcap = 0;
    for(size_t i = 0; i < n; i++){
        outcap += names[i].len;
    }
    // account for newlines and \0
    outcap += n + 1;
    char *buffer = malloc(outcap);
    if(!buffer){
        perror("malloc");
        return 1;
    }

    // write all the names into the buffer
    size_t len = 0;
    for(size_t i = 0; i < n; i++){
        size_t limit = outcap - len;
        size_t written = snprintf(&buffer[len], limit, "%s\n", names[i].text);
        if(written >= limit){
            // only possible with an arithmetic error
            fprintf(stderr, "join_names overran its buffer!\n");
            free(buffer);
            return 1;
        }
        len += written;
    }

    *out = (string_t){ .len=len, .text=buffer };
    return 0;
}


int manifest(const char *output, char *sep_cstr){
    int retval = 0;
    string_t in = {0};
    string_t *names = NULL;
    size_t names_len = 0;
    string_t sorted = {0};
    string_t old = {0};

    // read the list of names from stdin
    retval = read_stream(stdin, &in);
    fclose(stdin);
    if(retval) goto cu;

    char buffer[3];
    string_t sep;
    if(sep_cstr){
        sep = (string_t){
            .text = sep_cstr,
            .len=sep_cstr[0] == '\0' ? 1 : strlen(sep_cstr),
        };
    }else{
        sep = detect_endings(in, buffer);
    }

    // split names on newlines
    retval = split(in, sep, &names, &names_len);
    if(retval) goto cu;

    // sort the list of names
    qsort(names, names_len, sizeof(*names), cmp_string);

    retval = join_names(names, names_len, &sorted);
    if(retval) goto cu;

    // check if the output exists (try to stat() it)
    filetime_t output_info;
    int ret = get_filetime(output, &output_info);
    if(ret){
        if(ret != FILE_NOT_FOUND){
            compat_perror(output);
            retval = 1;
            goto cu;
        }
        // no output yet, write it now
        retval = write_file(output, sorted);
        goto cu;
    }

    // read the old file
    retval = read_file(output, &old);
    if(retval) goto cu;

    // compare contents
    if(!string_eq(old, sorted)){
        // contents differ; overwrite it
        retval = write_file(output, sorted);
        goto cu;
    }

    /* make sure that the output file has a modified-time that is at least as
       new as the latest matching file we found */
    for(size_t i = 0; i < names_len; i++){
        filetime_t info;
        ret = get_filetime(names[i].text, &info);
        if(ret){
            compat_perror(names[i].text);
            retval = 1;
            goto cu;
        }
        if(isnewer(info, output_info)){
            ret = compat_utime(output);
            if(ret){
                perror(output);
                retval = 1;
                goto cu;
            }
            // quit after the first new file is found
            goto cu;
        }
    }

cu:
    // free all of the names we collected
    free(names);
    free(in.text);
    free(sorted.text);
    free(old.text);
    return retval;
}


int print_help(FILE *f){
    fprintf(f, "usage: manifest [SEP] OUTPUT <filenames\n");
    fprintf(f, "where SEP may be one of: -0 -cr -lf -crlf -lfcr\n");
    fprintf(f, "when SEP is not provided, stdin is split on ");
    fprintf(f, "automatically-detected line endings\n");
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
    char *sep = NULL;
    char *output = NULL;
    bool nomoreflags = false;
    for(int i = 1; i < argc; i++){
        if(nomoreflags && output) return print_help(stderr);
        else if(nomoreflags) output = argv[i];
        else if(strcmp(argv[i], "-0") == 0) sep = "\0";
        else if(strcmp(argv[i], "-cr") == 0) sep = "\r";
        else if(strcmp(argv[i], "-lf") == 0) sep = "\n";
        else if(strcmp(argv[i], "-crlf") == 0) sep = "\r\n";
        else if(strcmp(argv[i], "-lfcr") == 0) sep = "\n\r";
        else if(strcmp(argv[i], "--help") == 0) return print_help(stdout);
        else if(strcmp(argv[i], "-h") == 0) return print_help(stdout);
        else if(strcmp(argv[i], "--version") == 0) return print_version();
        else if(strcmp(argv[i], "--") == 0) nomoreflags = true;
        else if(output) return print_help(stderr);
        else output = argv[i];
    }
    if(!output) return print_help(stderr);

    return manifest(output, sep);
}
