#include "findglob.c"

#include <fcntl.h>
#ifndef _WIN32 // UNIX
    #include <unistd.h>
#else // WINDOWS

#pragma warning(push, 0)
#include <windows.h>
#include <direct.h>
#include <io.h>
#pragma warning(pop)

// copied from splintermail project, then modified
// (https://github.com/splintermail/splintermail-client)
// original code was public domain under the UNLICENSE
int open(const char* pathname, int flags, ...){
    // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/sopen-s-wsopen-s

    // get the optional mode argument, or 0
    va_list ap;
    va_start(ap, flags);
    int mode = 0;
    if(flags & _O_CREAT){
        mode = va_arg(ap, int);
    }
    va_end(ap);

    // if we call posix open() we always want binary mode
    int oflag = _O_BINARY | flags;

    // no sharing of files
    int shflag = _SH_DENYRW;

    // windows-translated read/write permissions
    int pmode = 0;
    if(mode & 0444) pmode |= _S_IREAD;
    if(mode & 0222) pmode |= _S_IWRITE;

    int fdout = -1;

    // the global errno is also set, so we can ignore the return errno
    _sopen_s(&fdout, pathname, oflag, shflag, pmode);

    return fdout;
}
#endif

string_t S(char *text){
    return (string_t){ .text = text, .len = strlen(text) };
}

#define ASSERT(code) do { \
    if(!(code)){ \
        fprintf(stderr, "ASSERT(" #code ") failed\n"); \
        retval = 1; \
    } \
} while(0)

#define SWALLOW_STDERR \
    /* drop stderr for a test */ \
    fake_io_t fake_stderr; \
    fake_io(&fake_stderr, stderr, "test_stderr")

#define UNSWALLOW_STDERR \
    /* restore stderr after a test */ \
    string_t err = restore_io(&fake_stderr); \
    if(retval){ \
        fprintf(stderr, "%.*s", F(err)); \
    } \
    free(err.text)

typedef struct {
    FILE *f;
    char *tempfile;
    int backup_fd;
} fake_io_t;

// assumes errors are non-recoverable
void fake_io(fake_io_t *fake, FILE *f, char *tempfile){
    // create a regular file
    int temp_fd = open(tempfile, O_WRONLY|O_CREAT, 0666);
    if(temp_fd < 0){
        perror(fake->tempfile);
        exit(2);
    }

    // flush stdout before switching file descriptors
    int ret = fflush(f);
    if(ret){
        perror("fflush");
        exit(3);
    }

    // keep track of original fd, then close the original
    int backup_fd = dup(fileno(f));
    if(backup_fd < 0){
        perror("dup(fileno(f))");
        exit(4);
    }
    close(fileno(f));

    // dup our regular file to act as the original
    ret = dup(temp_fd);
    if(ret < 0){
        perror("dup(temp_fd)");
        exit(5);
    }
    close(temp_fd);

    *fake = (fake_io_t){
        .f = f,
        .tempfile = tempfile,
        .backup_fd = backup_fd,
    };
}

// assumes errors are non-recoverable, and you must free the return value
string_t restore_io(fake_io_t *fake){
    // flush fake stream
    int ret = fflush(fake->f);
    if(ret){
        perror("fflush");
        exit(6);
    }

    // close fake stream
    ret = close(fileno(fake->f));
    if(ret){
        perror("close");
        exit(7);
    }

    // restore original stream
    ret = dup(fake->backup_fd);
    if(ret < 0){
        perror("dup(backup_fd)");
        exit(8);
    }
    close(fake->backup_fd);

    // read output file
    FILE *f = fopen(fake->tempfile, "r");
    if(!f){
        perror(fake->tempfile);
        exit(9);
    }
    char *text = NULL;
    size_t len = 0;
    size_t cap = 0;
    while(true){
        cap += 4096;
        text = realloc(text, cap);
        if(!text){
            perror("realloc");
            exit(10);
        }
        size_t nread = fread(text, 1, 4096, f);
        if(nread == 0){
            if(feof(f)){
                break;
            }
            perror("fread");
            exit(11);
        }
        len += nread;
    }
    fclose(f);

    // remove the file
    ret = remove(fake->tempfile);
    if(ret){
        perror(fake->tempfile);
        exit(11);
    }

    return (string_t){ .text = text, .len = len };
}

int test_string(){
    int retval = 0;

    ASSERT(string_eq(S("abc"), S("abc")));
    ASSERT(!string_eq(S("abc"), S("abcd")));

    ASSERT(string_startswith(S("abc"), S("")));
    ASSERT(!string_startswith(S("abc"), S("z")));
    ASSERT(string_startswith(S("abc"), S("a")));
    ASSERT(string_startswith(S("abc"), S("ab")));
    ASSERT(!string_startswith(S("abc"), S("abcd")));

    ASSERT(string_endswith(S("abc"), S("")));
    ASSERT(!string_endswith(S("abc"), S("z")));
    ASSERT(string_endswith(S("abc"), S("c")));
    ASSERT(string_endswith(S("abc"), S("bc")));
    ASSERT(string_endswith(S("abc"), S("abc")));

    ASSERT(string_contains(S("abc"), S("")));
    ASSERT(string_contains(S("abc"), S("a")));
    ASSERT(!string_contains(S("abc"), S("z")));
    ASSERT(string_contains(S("abc"), S("ab")));
    ASSERT(string_contains(S("abc"), S("abc")));
    ASSERT(!string_contains(S("abc"), S("abcd")));
    ASSERT(string_contains(S("abc"), S("bc")));
    ASSERT(string_contains(S("abc"), S("c")));

    ASSERT(string_cmp(S("a"), S("ab")) == -1);
    ASSERT(string_cmp(S("ab"), S("a")) == 1);
    ASSERT(string_cmp(S("a"), S("a")) == 0);
    ASSERT(string_cmp(S("a"), S("b")) == -1);
    ASSERT(string_cmp(S("b"), S("a")) == 1);

    ASSERT(path_startswith(S("a/b"), S("a/b")));
    ASSERT(path_startswith(S("a/b/c"), S("a/b")));
    ASSERT(!path_startswith(S("a/bb"), S("a/b")));
    ASSERT(path_startswith(S("/a"), S("/")));

    return retval;
}

// this is the OPT_NONE matching logic
int test_glob_match(){
    int retval = 0;

    #define TEST_CASE(GLOB, LIT, TEXT, EXP) do { \
        bool lit[256]; \
        size_t i = 0; \
        for(char *l = LIT; *l; i++, l++){ \
            lit[i] = (*l == 't'); \
        } \
        if(glob_match(S(GLOB), lit, S(TEXT)) != EXP){ \
            fprintf( \
                stderr, \
                "TEST_CASE("#GLOB", "#LIT", "#TEXT", "#EXP") failed\n" \
                ); \
            retval = 1; \
        } \
    } while(0)

    TEST_CASE("*", "f", "asdf", true);
    TEST_CASE("a*b*c", "tftft", "abc", true);
    TEST_CASE("a*b*c", "tftft", "aabbcc", true);
    TEST_CASE("a*b*c", "tftft", "abbcaac", true);
    TEST_CASE("a*b*c", "tftft", "aasdfbbbbbbcccccc", true);
    TEST_CASE("a*b*c", "tttft", "abc", false);
    TEST_CASE("a*b*c", "tttft", "a*bc", true);
    TEST_CASE("a?c", "tft", "abc", true);
    TEST_CASE("a?c", "tft", "a?c", true);
    TEST_CASE("a?c", "ttt", "abc", false);
    TEST_CASE("a?c", "ttt", "a?c", true);

    return retval;
    #undef TEST_CASE
}

int path_iter_test_case(
    char *input,
    bool exp_vol,
    char **exp,
    size_t nexp
){
    string_t sects[32];
    size_t cap = sizeof(sects)/sizeof(*sects);
    size_t len = 0;
    size_t exp_i = 0;

    bool checked_vol = false;
    bool have_vol = false;
    path_iter_t it;
    for(string_t s = path_iter(&it, S(input)); it.ok; s = path_next(&it)){
        if(len == cap){
            fprintf(stderr, "too many sections!\n");
            return 1;
        }
        if(checked_vol && it.isvol){
            fprintf(
                stderr, "test_path_iter: got isvol after the first round!\n"
            );
            return 1;
        }
        if(!checked_vol){
            checked_vol = true;
            have_vol = it.isvol;
        }
        if(it.i != exp_i){
            fprintf(
                stderr,
                "test_path_iter: expected i=%zu but got i=%zu\n",
                exp_i,
                it.i
            );
            return 1;
        }
        sects[len++] = s;
        exp_i++;
    }
    // ensure that i was incremented once after completion
    if(it.i != exp_i){
        fprintf(
            stderr,
            "test_path_iter: afterwards, expected i=%zu but got i=%zu\n",
            exp_i,
            it.i
        );
        return 1;
    }


    int failures = !(len == nexp); // 1 = text, 2 = vol
    for(size_t i = 0; !failures && i < len; i++){
        if(string_eq(sects[i], S(exp[i]))) continue;
        failures |= 1;
        break;
    }
    if(exp_vol != have_vol) failures |= 2;

    if(failures){
        fprintf(stderr, "test_path_iter failed on input %s\n", input);
        if(failures & 1){
            fprintf(stderr, "expected: {");
            for(size_t i = 0; i < nexp; i++){
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%s", exp[i]);
            }
            fprintf(stderr, "}\nbut got:  {");
            for(size_t i = 0; i < len; i++){
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%.*s", (int)sects[i].len, sects[i].text);
            }
            fprintf(stderr, "}\n");
        }
        if(failures & 2){
            if(exp_vol){
                fprintf(stderr, "expected volume but didn't see it\n");
            }else{
                fprintf(stderr, "expected no volume but saw one\n");
            }
        }
    }
    return failures;
}

int test_path_iter(){
    int retval = 0;

    #define TEST_CASE(INPUT, EXP_VOL, ...) do { \
        char *exp[] = {NULL, __VA_ARGS__}; \
        size_t nexp = sizeof(exp)/sizeof(*exp); \
        int ret = path_iter_test_case(INPUT, EXP_VOL, &exp[1], nexp-1); \
        if(ret) retval = ret; \
    } while(0)

    TEST_CASE("a/b/c", false, "a", "b", "c");
    TEST_CASE("/a/b/c", true, "/", "a", "b", "c");
    TEST_CASE("", false);

    return retval;
    #undef TEST_CASE
}

int roots_iter_test_case(roots_iter_t *it, char **x, size_t nx){
    string_t strings[16];
    string_t *starts[16];
    size_t lengths[16];
    size_t nstarts = 0;
    size_t first = 0;
    // find all the NULL strings
    for(size_t i = 0; i < nx; i++){
        if(x[i] != NULL){
            strings[i] = S(x[i]);
            continue;
        }
        // found a NULL
        if(nstarts == 16){
            fprintf(stderr, "too many NULL dividers!\n");
            return 1;
        };
        lengths[nstarts] = i-first;
        starts[nstarts] = &strings[first];
        first = i+1;
        nstarts++;
    }
    if(nstarts < 2){
        fprintf(stderr, "need at least two NULL dividers!\n");
        return 1;
    }
    // build mock pattern_t's from the realpath group
    pattern_t patterns[16];
    size_t npatterns = lengths[0];
    string_t bang = S("!");
    for(size_t i = 0; i < npatterns; i++){
        patterns[i].anti = string_startswith(strings[i], bang);
        if(patterns[i].anti){
            patterns[i].start = string_sub(strings[i], 1, strings[i].len);
        }else{
            patterns[i].start = strings[i];
        }
    }

    // actually iterate through roots
    bool it_ok;
    size_t i;
    int failures = 0; // 1 = wrong, 2 = too many inputs, 4 = too man outputs
    for(
        i = 1, it_ok = roots_iter(it, patterns, npatterns);
        i < nstarts && it_ok;
        i++, it_ok = roots_next(it)
    ){
        string_t *exp = starts[i];
        size_t nexp = lengths[i];
        if(nexp != it->nmembers){
            failures = 1;
            break;
        }
        for(size_t j = 0; j < it->nmembers; j++){
            // expected values may begin with '!' to mark antipatterns
            string_t this_exp = exp[j];
            bool exp_anti = string_startswith(this_exp, bang);
            bool got_anti = patterns[it->members[j]].anti;
            if(exp_anti != got_anti){
                failures |= 1;
                break;
            }
            // do the string comparison without the '!'
            if(exp_anti) this_exp = string_sub(this_exp, 1, this_exp.len);
            if(!string_eq(this_exp, patterns[it->members[j]].start)){
                failures |= 1;
                break;
            }
        }
        if(failures) break;
    }
    if(!failures){
        if(i != nstarts){ failures |= 2; }
        if(it_ok){ failures |= 4; }
    }
    if(!failures) return 0;

    fprintf(stderr, "test_roots_iter() failed, inputs = {");
    for(size_t j = 0; j < npatterns; j++){
        if(j) fprintf(stderr, ", ");
        fprintf(stderr, "%.*s", F(patterns[j].start));
    }
    fprintf(stderr, "}\n");
    if(failures & 1){
        fprintf(stderr, "expected group[%zu] = {", i-1);
        for(size_t j = 0; j < lengths[i]; j++){
            if(j) fprintf(stderr, ", ");
            fprintf(stderr, "%.*s", F(starts[i][j]));
        }
        fprintf(stderr, "}\n");
        fprintf(stderr, "but got  group[%zu] = {", i-1);
        for(size_t j = 0; j < it->nmembers; j++){
            if(j) fprintf(stderr, ", ");
            if(patterns[it->members[j]].anti) fprintf(stderr, "!");
            fprintf(stderr, "%.*s", F(patterns[it->members[j]].start));
        }
        fprintf(stderr, "}\n");
    }
    return 1;
}

int test_roots_iter(){
    int retval = 0;
    roots_iter_t it;

    // vaargs here is a series of NULL-teriminated lists
    // REALPATH... NULL (GROUP... NULL)...
    //
    #define TEST_CASE(RP1, ...) do { \
        char *x[] = {RP1, __VA_ARGS__}; \
        size_t nx = sizeof(x)/sizeof(*x); \
        int ret = roots_iter_test_case(&it, x, nx); \
        if(ret) retval = ret; \
    } while(0)

    // a is parent of b
    TEST_CASE(
        /* REALPATHS */ "/a/b", "/a/b/c", NULL,
        /* GROUP */ "/a/b", "/a/b/c", NULL
    );

    // a is parent of b (a is bare volume)
    TEST_CASE(
        /* REALPATHS */ "/", "/a/b/c", NULL,
        /* GROUP */ "/", "/a/b/c", NULL
    );

    // b is parent of a
    TEST_CASE(
        /* REALPATHS */ "/a/b/c", "/a/b", NULL,
        /* GROUP */ "/a/b", "/a/b/c", NULL
    );

    // b is parent of a (b is bare volume)
    TEST_CASE(
        /* REALPATHS */ "/a/b/c", "/", NULL,
        /* GROUP */ "/", "/a/b/c", NULL
    );

    // a and b are peers (b startswith a)
    TEST_CASE(
        /* REALPATHS */ "/a/b", "/a/bb", NULL,
        /* GROUP */ "/a/b", NULL,
        /* GROUP */ "/a/bb", NULL
    );

    // a and b are peers (a startswith b)
    TEST_CASE(
        /* REALPATHS */ "/a/bb", "/a/b", NULL,
        /* GROUP */ "/a/bb", NULL,
        /* GROUP */ "/a/b", NULL
    );

    // multiple groups, each with some nesting
    TEST_CASE(
        /* REALPATHS */ "/a", "/a/b", "/b/c", "/b", NULL,
        /* GROUP */ "/a", "/a/b", NULL,
        /* GROUP */ "/b", "/b/c", NULL
    );

    // a == b
    TEST_CASE(
        /* REALPATHS */ "/a/b", "/a/b/c", "/a/b", NULL,
        /* GROUP */ "/a/b", "/a/b/c", "/a/b", NULL
    );

    // a == b, but b is anti
    TEST_CASE(
        /* REALPATHS */ "/a/b", "/a/b/c", "!/a/b", NULL,
        /* GROUP */ "/a/b", "/a/b/c", "!/a/b", NULL
    );

    // a == b, but a is anti
    TEST_CASE(
        /* REALPATHS */ "!/a/b", "/a/b/c", "/a/b", NULL,
        /* GROUP */ "/a/b", "!/a/b", "/a/b/c", NULL
    );

    // antipattern is always included, even when it's not nested
    TEST_CASE(
        /* REALPATHS */ "/a", "!/b", NULL,
        /* GROUP */ "/a", "!/b", NULL
    );

    return retval;
    #undef TEST_CASE
}

int section_parse_test_case(
    char *input,
    int exp_ret,
    section_e exp_type,
    opt_e exp_opt,
    char *exp_text,
    char *exp_text2,
    char *exp_lit
){
    string_t s = { .text = input, .len = strlen(input) };
    section_t sect;
    int got = section_parse(&sect, s);
    if(got != exp_ret){
        fprintf(
            stderr,
            "section_parse(%s) failed, expected %d but got %d\n",
            input,
            exp_ret,
            got
        );
        goto fail;
    }
    if(got) return 0;

    if(sect.type != exp_type){
        fprintf(
            stderr,
            "section_parse(%s) failed, expected .type=%d but got %d\n",
            input,
            exp_type,
            sect.type
        );
        goto fail;
    }

    switch(sect.type){
        case SECTION_ANY:
            goto pass;

        case SECTION_CONSTANT:
            if(!exp_text){
                fprintf(
                    stderr,
                    "bad test case (%s): SECTION_CONSTANT requires exp_text\n",
                    input
                );
                goto fail;
            }
            string_t exp_s = { .text = exp_text, .len = strlen(exp_text) };
            if(!string_eq(exp_s, sect.val.constant)){
                fprintf(
                    stderr,
                    "section_parse(%s) failed, expected .constant=%s but "
                    "got %.*s\n",
                    input,
                    exp_text,
                    (int)sect.val.constant.len, sect.val.constant.text
                );
            }
            goto pass;

        case SECTION_GLOB:
            // fallthru
            break;
    }

    // SECTION_GLOB
    glob_t glob = sect.val.glob;
    if(glob.opt != exp_opt){
        fprintf(
            stderr,
            "section_parse(%s) failed, expected .glob.opt=%d but got %d\n",
            input,
            exp_opt,
            glob.opt
        );
        goto fail;
    }
    if(glob.opt != OPT_ANY){
        // only OPT_ANY does not check text
        string_t exp_s = { .text = exp_text, .len = strlen(exp_text) };
        if(!string_eq(exp_s, glob.text)){
            fprintf(
                stderr,
                "section_parse(%s) failed, expected .glob.text='%s' but "
                "got '%.*s'\n",
                input,
                exp_text,
                (int)glob.text.len, glob.text.text
            );
            goto fail;
        }
    }
    if(glob.opt == OPT_NONE){
        // only OPT_NONE checks exp_lit
        if(!exp_lit){
            fprintf(
                stderr,
                "bad test case (%s): OPT_NONE requires exp_lit\n",
                input
            );
            goto fail;
        }
        if(strlen(exp_lit) != strlen(input)){
            fprintf(
                stderr, "bad test case (%s): exp_lit wrong length\n", input
            );
            goto fail;
        }
        char buf[PATH_MAX];
        for(size_t i = 0; i < glob.text.len; i++){
            buf[i] = glob.lit[i] ? 't' : 'f';
        }
        buf[glob.text.len] = '\0';
        if(strcmp(buf, exp_lit) != 0){
            fprintf(
                stderr,
                "section_parse(%s) failed, expected .glob.lit='%s' but "
                "got '%s'\n",
                input,
                exp_lit,
                buf
            );
            goto fail;
        }
    }
    if(glob.opt == OPT_BOOKENDS){
        // only OPT_BOOKENDS checks exp_text2
        if(!exp_text2){
            fprintf(
                stderr,
                "bad test case (%s): OPT_BOOKENDS requires exp_text2\n",
                input
            );
            goto fail;
        }
        string_t exp_s = { .text = exp_text2, .len = strlen(exp_text2) };
        if(!string_eq(exp_s, glob.text2)){
            fprintf(
                stderr,
                "section_parse(%s) failed, expected .glob.text2='%s' but "
                "got '%.*s'\n",
                input,
                exp_text2,
                (int)glob.text2.len, glob.text2.text
            );
            goto fail;
        }
    }

pass:
    section_free(&sect);
    return 0;

fail:
    section_free(&sect);
    return 1;
}

int test_section_parse(){
    int retval = 0;

    #define TEST_CASE(INPUT, RET, TYPE, OPT, TEXT, TEXT2, LIT) do { \
        int ret = section_parse_test_case( \
            INPUT, RET, SECTION_##TYPE, OPT, TEXT, TEXT2, LIT \
        ); \
        if(ret) retval = ret; \
    } while(0)

    #define FAIL_CASE(INPUT, RET) do { \
        int ret = section_parse_test_case( \
            INPUT, RET, 0, 0, NULL, NULL, NULL \
        ); \
        if(ret) retval = ret; \
    } while(0)

    SWALLOW_STDERR;

    // ** and double-* filtering
    TEST_CASE("**", 0, ANY, 0, NULL, NULL, NULL);
    FAIL_CASE("a**", 1);
    FAIL_CASE("**a", 1);
    TEST_CASE("*\\*\\**", 0, GLOB, OPT_CONTAINS, "**", NULL, NULL);

    // single *
    TEST_CASE("*", 0, GLOB, OPT_ANY, NULL, NULL, NULL);
    TEST_CASE("\\*", 0, CONSTANT, 0, "*", NULL, NULL);

    // simple constant
    TEST_CASE("abc", 0, CONSTANT, 0, "abc", NULL, NULL);
    TEST_CASE("a?bc", 0, GLOB, OPT_NONE, "a?bc", NULL, "tftt");

    // CONTAINS
    TEST_CASE("*abc*", 0, GLOB, OPT_CONTAINS, "abc", NULL, NULL);
    TEST_CASE("*a?c*", 0, GLOB, OPT_NONE, "*a?c*", NULL, "ftftf");

    // SUFFIX
    TEST_CASE("*abc", 0, GLOB, OPT_SUFFIX, "abc", NULL, NULL);
    TEST_CASE("*a?bc", 0, GLOB, OPT_NONE, "*a?bc", NULL, "ftftt");
    TEST_CASE("*a\\?bc", 0, GLOB, OPT_SUFFIX, "a?bc", NULL, NULL);

    // PREFIX
    TEST_CASE("abc*", 0, GLOB, OPT_PREFIX, "abc", NULL, NULL);
    TEST_CASE("a?bc*", 0, GLOB, OPT_NONE, "a?bc*", NULL, "tfttf");

    // BOOKENDS
    TEST_CASE("ab*cd", 0, GLOB, OPT_BOOKENDS, "ab", "cd", NULL);
    TEST_CASE("a*b*c*d", 0, GLOB, OPT_NONE, "a*b*c*d", NULL, "tftftft");

    UNSWALLOW_STDERR;

    return retval;
    #undef TEST_CASE
    #undef FAIL_CASE
}

static char *ANY = "ANY";
int pattern_parse_test_case(
    char *input,
    int exp_ret,
    char *exp_start,
    bool anti,
    char **exp,
    size_t nexp
){
    pattern_t pattern;
    int got = pattern_parse(&pattern, input);
    if(got != exp_ret){
        fprintf(
            stderr,
            "test pattern parse case '%s' failed, expected %d but got %d\n",
            input,
            exp_ret,
            got
        );
        pattern_free(&pattern);
        return 1;
    }
    if(got){
        pattern_free(&pattern);
        return 0;
    }
    int failures = !(pattern.len == nexp); // 1 = text, 2 = other
    if(anti != pattern.anti){
        fprintf(
            stderr,
            "test pattern parse case '%s' failed, "
            "expected anti=%s but got anti=%s\n",
            input,
            anti ? "true" : "false",
            pattern.anti ? "true" : "false"
        );
        failures |= 2;
    }
    if(!string_eq(pattern.start, S(exp_start))){
        fprintf(
            stderr,
            "test pattern parse case '%s' failed, "
            "expected start=%s but got start=%.*s\n",
            input,
            exp_start,
            (int)pattern.start.len, pattern.start.text
        );
        failures |= 2;
    }
    for(size_t i = 0; !failures && i < pattern.len; i++){
        // if the ANY pointer is used, the section must be the SECTION_ANY type
        section_t sect = pattern.sects[i];
        string_t expstr = { .text = exp[i], .len = strlen(exp[i]) };
        switch(sect.type){
            case SECTION_ANY:
                failures |= !(exp[i] == ANY);
                break;
            case SECTION_CONSTANT:
                failures |= !string_eq(sect.val.constant, expstr);
                break;
            case SECTION_GLOB:
                // we don't gain any coverage re-testing section_parse() here.
                fprintf(stderr, "SECTION_GLOB not handled\n");
                failures |= 1;
                break;
        }
    }
    if(failures & 1){
        fprintf(
            stderr,
            "test pattern parse case '%s' failed, expected:\n    ",
            input
        );
        for(size_t i = 0; i < nexp; i++){
            fprintf(stderr, "%s%s", i==0 ? "" : ", ", exp[i]);
        }
        fprintf(stderr, "\nbut got:\n    ");
        for(size_t i = 0; i < pattern.len; i++){
            section_t sect = pattern.sects[i];
            if(i) fprintf(stderr, ", ");
            switch(sect.type){
                case SECTION_ANY:
                    fprintf(stderr, "ANY");
                    break;
                case SECTION_CONSTANT:
                    fprintf(stderr, "%.*s", F(sect.val.constant));
                    break;
                case SECTION_GLOB:
                    fprintf(stderr, "SECTION_GLOB not handled\n");
                    break;
            }
        }
        fprintf(stderr, "\n");
    }
    pattern_free(&pattern);
    return failures;
}

int test_pattern_parse(){
    int retval = 0;

    #define TEST_CASE(INPUT, EXP_RET, START, ANTI, ...) do { \
        char *exp[] = {__VA_ARGS__}; \
        size_t nexp = sizeof(exp)/sizeof(*exp); \
        int ret = pattern_parse_test_case( \
            INPUT, EXP_RET, START, ANTI, exp, nexp \
        ); \
        if(ret) retval = ret; \
    } while(0)

    SWALLOW_STDERR;

    // absolute path
    TEST_CASE("/asdf/**/zxcv", 0, "/asdf", false, "/", "asdf", ANY, "zxcv");
    // relative path
    TEST_CASE("asdf/**/zxcv", 0, "asdf", false, "asdf", ANY, "zxcv");
    // consecutive doublestar
    TEST_CASE("**/**", 1, ".", false, "x");
    // anti abs path
    TEST_CASE("!/asdf/**/zxcv", 0, "/asdf", true, "/", "asdf", ANY, "zxcv");
    // anti rel path
    TEST_CASE("!asdf/**/zxcv", 0, "asdf", true, "asdf", ANY, "zxcv");

    // used parsed section output to create pattern.start
    TEST_CASE("a\\*b/**", 0, "a*b", false, "a*b", ANY);

    // regression cases
    TEST_CASE("/**", 0, "/", false, "/", ANY);
    TEST_CASE("/a/**", 0, "/a", false, "/", "a", ANY);

    UNSWALLOW_STDERR;

    return retval;
    #undef TEST_CASE
}

int pattern_rewrite_start_test_case(
    char *input,
    char *rewrite,
    char **exp,
    size_t nexp
){
    int failures = 0; // 1 = text, 2 = other
    pattern_t pattern;
    int ret = pattern_parse(&pattern, input);
    if(ret){
        fprintf(
            stderr,
            "test pattern rewrite case '%s' -> '%s' failed to parse\n",
            input,
            rewrite
        );
        failures |= 2;
        goto cu;
    }
    ret = pattern_rewrite_start(&pattern, S(rewrite));
    if(ret){
        fprintf(
            stderr,
            "test pattern rewrite case '%s' -> '%s' failed to rewrite\n",
            input,
            rewrite
        );
        failures |= 2;
        goto cu;
    }
    failures |= !(pattern.len == nexp);
    if(!string_eq(pattern.start, S(rewrite))){
        fprintf(
            stderr,
            "test pattern rewrite case '%s' -> '%s' failed, "
            "expected start=%s but got start=%.*s\n",
            input, rewrite, rewrite, F(pattern.start)
        );
        failures |= 2;
    }
    for(size_t i = 0; !failures && i < pattern.len; i++){
        // if the ANY pointer is used, the section must be the SECTION_ANY type
        section_t sect = pattern.sects[i];
        string_t expstr = { .text = exp[i], .len = strlen(exp[i]) };
        switch(sect.type){
            case SECTION_ANY:
                failures |= !(exp[i] == ANY);
                break;
            case SECTION_CONSTANT:
                failures |= !string_eq(sect.val.constant, expstr);
                break;
            case SECTION_GLOB:
                // we don't gain any coverage re-testing section_parse() here.
                fprintf(stderr, "SECTION_GLOB not handled\n");
                failures |= 1;
                break;
        }
    }
    if(failures & 1){
        fprintf(
            stderr,
            "test pattern rewrite case '%s' -> '%s' failed, expected:\n    ",
            input, rewrite
        );
        for(size_t i = 0; i < nexp; i++){
            if(i) fprintf(stderr, ", ");
            fprintf(stderr, "%s", exp[i]);
        }
        fprintf(stderr, "\nbut got:\n    ");
        for(size_t i = 0; i < pattern.len; i++){
            section_t sect = pattern.sects[i];
            if(i) fprintf(stderr, ", ");
            switch(sect.type){
                case SECTION_ANY:
                    fprintf(stderr, "ANY");
                    break;
                case SECTION_CONSTANT:
                    fprintf(stderr, "%.*s", F(sect.val.constant));
                    break;
                case SECTION_GLOB:
                    fprintf(stderr, "(SECTION_GLOB not handled)");
                    break;
            }
        }
        fprintf(stderr, "\n");
    }
cu:
    pattern_free(&pattern);
    return failures;
}

int test_pattern_rewrite_start(){
    int retval = 0;

    #define TEST_CASE(INPUT, REWRITE, ...) do { \
        char *exp[] = {__VA_ARGS__}; \
        size_t nexp = sizeof(exp)/sizeof(*exp); \
        int ret = pattern_rewrite_start_test_case( \
            INPUT, REWRITE, exp, nexp \
        ); \
        if(ret) retval = ret; \
    } while(0)

    // simple cases
    TEST_CASE("b/**", "/a/b", /* -> */ "/", "a", "b", ANY);
    TEST_CASE("b/**/c", "/a/b", /* -> */ "/", "a", "b", ANY, "c");
    // implicit dot cases
    TEST_CASE("**", "/a/b", /* -> */ "/", "a", "b", ANY);
    TEST_CASE("**/c", "/a/b", /* -> */ "/", "a", "b", ANY, "c");
    // .. case
    TEST_CASE("../**/c", "/a/b", /* -> */ "/", "a", "b", ANY, "c");
    // shortening case (not realistic, but still a codepath
    TEST_CASE("/a/b/**/c", "a", /* -> */ "a", ANY, "c");
    // identity case
    TEST_CASE("/a/b/**/c", "/a/b", /* -> */ "/", "a", "b", ANY, "c");
    // all-const cases
    TEST_CASE("/a/b/c", "/a/b", /* -> */ "/", "a", "b");
    TEST_CASE("/a/b/c", "/a/b/c", /* -> */ "/", "a", "b", "c");
    TEST_CASE("/a/b/c", "/a/b/c/d", /* -> */ "/", "a", "b", "c", "d");

    return retval;
    #undef TEST_CASE
}

int match_text_case(
    char *pattern_in, char *term, class_e class, match_flags_e exp
){
    pattern_t pattern;
    pattern_parse(&pattern, pattern_in);
    match_t match = { .pattern = &pattern, .matched = 0 };
    string_t text = { .text = term, .len = strlen(term) };
    match_flags_e flags = match_text(match, text, class);
    pattern_free(&pattern);
    if(flags == exp) return 0;
    // failure case
    fprintf(
        stderr,
        "test match text '%s' against '%s' failed, expected ",
        term,
        pattern_in
    );
    int comma = 0;
    if(exp & MATCH_0){
        fprintf(stderr, "MATCH_0");
        comma++;
    }
    if(exp & MATCH_1)
        fprintf(stderr, "%sMATCH_1", comma++ ? "|" : "");
    if(exp & MATCH_2)
        fprintf(stderr, "%sMATCH_2", comma++ ? "|" : "");
    if(exp & MATCH_TERMINAL)
        fprintf(stderr, "%sMATCH_TERMINAL", comma++ ? "|" : "");
    if(!comma) fprintf(stderr, "MATCH_NONE");
    fprintf(stderr, " but got ");
    comma = 0;
    if(flags & MATCH_0){
        fprintf(stderr, "MATCH_0");
        comma++;
    }
    if(flags & MATCH_1)
        fprintf(stderr, "%sMATCH_1", comma++ ? "|" : "");
    if(flags & MATCH_2)
        fprintf(stderr, "%sMATCH_2", comma++ ? "|" : "");
    if(flags & MATCH_TERMINAL)
        fprintf(stderr, "%sMATCH_TERMINAL", comma++ ? "|" : "");
    if(!comma) fprintf(stderr, "MATCH_NONE");
    fprintf(stderr, "\n");
    return 1;
}

int test_match_text(){
    int retval = 0;

    #define TEST_CASE(PATTERN, TERM, CLASS, EXP) do { \
        int ret = match_text_case(PATTERN, TERM, CLASS, EXP); \
        if(ret) retval = ret; \
    } while(0)

    TEST_CASE("x", "a", CLASS_DIR, MATCH_NONE);
    TEST_CASE("a", "a", CLASS_DIR, MATCH_TERMINAL);
    TEST_CASE("a/x", "a", CLASS_DIR, MATCH_1);
    TEST_CASE("a/**", "a", CLASS_DIR, MATCH_1|MATCH_TERMINAL);
    TEST_CASE("a/**/x", "a", CLASS_DIR, MATCH_1);
    TEST_CASE("**", "a", CLASS_DIR, MATCH_0|MATCH_TERMINAL);
    TEST_CASE("**/a", "a", CLASS_DIR, MATCH_0|MATCH_TERMINAL);
    TEST_CASE("**/a/**", "a", CLASS_DIR, MATCH_2|MATCH_TERMINAL);
    TEST_CASE("**/a/**/x", "a", CLASS_DIR, MATCH_2);
    TEST_CASE("**/a/x", "a", CLASS_DIR, MATCH_0|MATCH_2);
    TEST_CASE("**/x", "a", CLASS_DIR, MATCH_0);

    // simulate matching /**/b/** against /a/b/c
    TEST_CASE("/**/b/**", "/", CLASS_DIR, MATCH_1);
    TEST_CASE("**/b/**", "a", CLASS_DIR, MATCH_0);
    TEST_CASE("**/b/**", "b", CLASS_DIR, MATCH_2|MATCH_TERMINAL);
    TEST_CASE("**", "c", CLASS_DIR, MATCH_0|MATCH_TERMINAL);

    // regression cases
    TEST_CASE("/**", "/", CLASS_DIR, MATCH_1|MATCH_TERMINAL);

    // test TERMINAL behaviors with various types and inputs
    TEST_CASE("a/", "a", CLASS_FILE, MATCH_NONE);
    TEST_CASE(":d:a", "a", CLASS_FILE, MATCH_NONE);
    TEST_CASE(":f:a", "a", CLASS_DIR, MATCH_NONE);
    TEST_CASE("a/**", "a", CLASS_FILE, MATCH_1);
    TEST_CASE("**/", "a", CLASS_FILE, MATCH_0);
    TEST_CASE(":d:**", "a", CLASS_FILE, MATCH_0);
    TEST_CASE(":f:**", "a", CLASS_FILE, MATCH_0|MATCH_TERMINAL);
    TEST_CASE(":f:**", "a", CLASS_DIR, MATCH_0);
    TEST_CASE(":fd:**", "a", CLASS_FILE, MATCH_0|MATCH_TERMINAL);
    TEST_CASE("**/a/", "a", CLASS_FILE, MATCH_0);
    TEST_CASE(":d:**/a", "a", CLASS_FILE, MATCH_0);
    TEST_CASE(":df:**/a", "a", CLASS_FILE, MATCH_0|MATCH_TERMINAL);
    TEST_CASE(":f:**/a", "a", CLASS_FILE, MATCH_0|MATCH_TERMINAL);
    TEST_CASE(":f:**/a", "a", CLASS_DIR, MATCH_0);
    TEST_CASE("**/a/**", "a", CLASS_FILE, MATCH_2);

    // classmatch && isdir cases
    TEST_CASE("**/a/**", "a", CLASS_DIR, MATCH_2|MATCH_TERMINAL);
    TEST_CASE(":d:**/a/**", "a", CLASS_DIR, MATCH_2|MATCH_TERMINAL);
    TEST_CASE(":f:**/a/**", "a", CLASS_DIR, MATCH_2);
    TEST_CASE("a/**", "a", CLASS_DIR, MATCH_1|MATCH_TERMINAL);
    TEST_CASE(":d:a/**", "a", CLASS_DIR, MATCH_1|MATCH_TERMINAL);
    TEST_CASE(":f:a/**", "a", CLASS_DIR, MATCH_1);


    return retval;
    #undef TEST_CASE
}

void sprint_pattern(char *buf, pattern_t pattern, size_t skip){
    char *bufp = buf;
    if(pattern.anti) bufp += sprintf(bufp, "!");
    for(size_t i = 0; i + skip < pattern.len; i++){
        if(i > 1 || (i == 1 && !_is_sep(*(bufp-1)))){
            bufp += sprintf(bufp, "/");
        }
        section_t sect = pattern.sects[i + skip];
        switch(sect.type){
            case SECTION_ANY:
                bufp += sprintf(bufp, "**");
                break;

            case SECTION_CONSTANT:
                bufp += sprintf(
                    bufp,
                    "%.*s",
                    (int)sect.val.constant.len,
                    sect.val.constant.text
                );
                break;

            case SECTION_GLOB:
                switch(sect.val.glob.opt){
                    case OPT_ANY:
                        *(bufp++) = '*';
                        break;
                    case OPT_PREFIX:
                        bufp += sprintf(
                            bufp,
                            "%.*s",
                            (int)sect.val.glob.text.len,
                            sect.val.glob.text.text
                        );
                        *(bufp++) = '*';
                        break;
                    case OPT_SUFFIX:
                        *(bufp++) = '*';
                        bufp += sprintf(
                            bufp,
                            "%.*s",
                            (int)sect.val.glob.text.len,
                            sect.val.glob.text.text
                        );
                        break;
                    case OPT_BOOKENDS:
                        bufp += sprintf(
                            bufp,
                            "%.*s",
                            (int)sect.val.glob.text.len,
                            sect.val.glob.text.text
                        );
                        *(bufp++) = '*';
                        bufp += sprintf(
                            bufp,
                            "%.*s",
                            (int)sect.val.glob.text2.len,
                            sect.val.glob.text2.text
                        );
                        break;
                    case OPT_CONTAINS:
                        *(bufp++) = '*';
                        bufp += sprintf(
                            bufp,
                            "%.*s",
                            (int)sect.val.glob.text.len,
                            sect.val.glob.text.text
                        );
                        *(bufp++) = '*';
                        break;
                    case OPT_NONE:
                        for(size_t j = 0; j < sect.val.glob.text.len; j++){
                            char c = sect.val.glob.text.text[j];
                            bool t = sect.val.glob.lit[j];
                            if((c == '*' || c == '?' || c == '\\') && t){
                                *(bufp++) = '\\';
                            }
                            *(bufp++) = c;
                        }
                        break;
                }
                break;
        }
    }
}

int process_dir_case(
    pool_t **p,
    match_array_t **ma,
    char **in,
    size_t nin,
    const string_t name,
    char **exp,
    size_t nexp,
    bool expintermediate,
    bool expterminal
){
    bool ok = true;

    pattern_t *patterns = malloc(nin * sizeof(*patterns));
    if(!patterns){
        perror("malloc");
        exit(1);
    }
    memset(patterns, 0, nin * sizeof(*patterns));

    match_array_t *matches_in = match_array_get(p, ma, 32);
    for(size_t i = 0; i < nin; i++){
        int ret = pattern_parse(&patterns[i], in[i]);
        if(ret){
            fprintf(stderr, "failed to parse input pattern '%s'\n", in[i]);
            ok = false;
            goto fail_parse_pattern;
        }
        match_t match = { .pattern = &patterns[i], .matched = 0 };
        match_array_add(matches_in, match);
    }

    bool isintermediate, isterminal;
    match_array_t *matches_out = match_array_get(p, ma, 32);
    process_dir(name, matches_in, matches_out, &isintermediate, &isterminal);

    int failures = 0; // 1 = patterns, 2 = intermediate, 4 = terminal
    if(nexp != matches_out->len){
        ok = false;
        failures |= 1;
    }
    for(size_t i = 0; ok && i < nexp; i++){
        match_t match = matches_out->items[i];
        char buf[1024];
        sprint_pattern(buf, *match.pattern, match.matched);
        if(strcmp(buf, exp[i]) != 0){
            ok = false;
            failures |= 1;
        }
    }
    if(expintermediate != isintermediate){
        ok = false;
        failures |= 2;
    }
    if(expterminal != isterminal){
        ok = false;
        failures |= 4;
    }
    if(!ok){
        fprintf(
            stderr,
            "process_dir failed matching '%.*s' against {",
            (int)name.len,
            name.text
        );
        for(size_t i = 0; i < nin; i++){
            if(i) fprintf(stderr, ", ");
            fprintf(stderr, "%s", in[i]);
        }
        fprintf(stderr, "}\n");
        if(failures & 1){
            fprintf(stderr, "expected:\n    ");
            for(size_t i = 0; i < nexp; i++){
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%s", exp[i]);
            }
            fprintf(stderr, "\nbut got:\n    ");
            for(size_t i = 0; i < matches_out->len; i++){
                match_t match = matches_out->items[i];
                char buf[1024];
                sprint_pattern(buf, *match.pattern, match.matched);
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%s", buf);
            }
            fprintf(stderr, "\n");
        }
        if(failures & 2){
            fprintf(
                stderr,
                "expected isintermediate=%s but got %s\n",
                expintermediate ? "true" : "false",
                isintermediate ? "true" : "false"
            );
        }
        if(failures & 4){
            fprintf(
                stderr,
                "expected isterminal=%s but got %s\n",
                expterminal ? "true" : "false",
                isterminal ? "true" : "false"
            );
        }
    }

    match_array_put(ma, matches_out);
fail_parse_pattern:
    for(size_t i = 0; i < matches_in->len; i++){
        pattern_free(&patterns[i]);
    }
    free(patterns);
    match_array_put(ma, matches_in);
    return !ok;
}

int test_process_dir(){
    int retval = 0;

    pool_t *p = NULL;
    match_array_t *ma = NULL;

    // the vararg is PATTERN_IN... NULL [PATTERN_OUT...]
    #define TEST_CASE(TERM, INTERMEDIATE, TERMINAL, ...) do { \
        char *strs[] = {__VA_ARGS__}; \
        size_t nstrs = sizeof(strs)/sizeof(*strs); \
        /* find the NULL string */ \
        size_t nullpos = (size_t)-1; \
        for(size_t i = 0; i < nstrs; i++){ \
            if(strs[i] == NULL){ \
                nullpos = i; \
                break; \
            } \
        } \
        if(nullpos == (size_t)-1){ \
            fprintf(stderr, "no NULL divider in input strings!\n"); \
            goto cu; \
        } \
        char **in = strs; \
        size_t nin = nullpos; \
        char **exp = &strs[nullpos + 1]; \
        size_t nexp = nstrs - nullpos - 1; \
        string_t name = { .text = TERM, .len = strlen(TERM) }; \
        int ret = process_dir_case( \
            &p, &ma, in, nin, name, exp, nexp, INTERMEDIATE, TERMINAL \
        ); \
        if(ret) retval = ret; \
    } while(0)


    // simulate matching /**/code/** against /home/user/code/mkninja/findglob
    TEST_CASE("/", true, false, "/**/code/**", NULL ,"**/code/**");
    TEST_CASE("home", true, false, "**/code/**", NULL, "**/code/**");
    TEST_CASE("user", true, false, "**/code/**", NULL, "**/code/**");
    TEST_CASE("code", true, true, "**/code/**", NULL, "**");
    TEST_CASE("mkninja", true, true, "**", NULL, "**");
    TEST_CASE("findglob", true, true, "**", NULL, "**");

    // simulate matching **/a/b against a/b/c
    TEST_CASE("a", true, false, "**/a/b", NULL, "**/a/b", "b");
    TEST_CASE("b", true, false, "**/a/b", NULL, "**/a/b");
    TEST_CASE("b", false, true, "b", NULL);
    TEST_CASE("b", true, true, "**/a/b", "b", NULL, "**/a/b");
    TEST_CASE("c", true, false, "**/a/b", NULL, "**/a/b");

    // simulate matching **, !**/b against a/b/c
    TEST_CASE("a", true, true, "!**/b", "**", NULL, "!**/b", "**");
    TEST_CASE("b", false, false, "!**/b", "**", NULL);

    // simulate matching :f:/** against /a/b/c/
    TEST_CASE("/", true, false, ":f:/**", NULL, "**");
    TEST_CASE("a", true, false, ":f:**", NULL, "**");
    TEST_CASE("b", true, false, ":f:**", NULL, "**");
    TEST_CASE("c", true, false, ":f:**", NULL, "**");

cu:
    match_array_free(&ma);
    pool_free(&p);
    return retval;
    #undef TEST_CASE
}

int matches_init_test_case(
    pool_t **p,
    match_array_t **ma,
    char **in,
    size_t nin,
    string_t start,
    char **exp,
    size_t nexp,
    bool expterminal
){
    bool ok = true;

    pattern_t *patterns = malloc(nin * sizeof(*patterns));
    if(!patterns){
        perror("malloc");
        exit(1);
    }
    memset(patterns, 0, nin * sizeof(*patterns));

    for(size_t i = 0; i < nin; i++){
        int ret = pattern_parse(&patterns[i], in[i]);
        if(ret){
            fprintf(stderr, "failed to parse input pattern '%s'\n", in[i]);
            ok = false;
            goto fail_parse_pattern;
        }
        if(patterns[i].start.len && _is_sep(patterns[i].start.text[0]))
            continue;
        // rewrite non-absolute paths to start at /pwd/
        ret = pattern_rewrite_start(&patterns[i], S("/pwd"));
        if(ret){
            fprintf(stderr, "failed to rewrite pattern start '%s'\n", in[i]);
            ok = false;
            goto fail_parse_pattern;
        }
    }

    qsort_patterns(patterns, nin);

    bool isterminal;
    match_array_t *matches_out = matches_init(
        p, ma, patterns, nin, start, start, &isterminal
    );

    int failures = 0; // 1 = patterns, 2 = terminal
    if(nexp != matches_out->len){
        ok = false;
        failures |= 1;
    }
    for(size_t i = 0; ok && i < nexp; i++){
        match_t match = matches_out->items[i];
        char buf[1024];
        sprint_pattern(buf, *match.pattern, match.matched);
        if(strcmp(buf, exp[i]) != 0){
            ok = false;
            failures |= 1;
        }
    }
    if(expterminal != isterminal){
        ok = false;
        failures |= 2;
    }
    if(!ok){
        fprintf(
            stderr,
            "matches_init failed matching '%.*s' against {",
            (int)start.len,
            start.text
        );
        for(size_t i = 0; i < nin; i++){
            if(i) fprintf(stderr, ", ");
            fprintf(stderr, "%s", in[i]);
        }
        fprintf(stderr, "}\n");
        if(failures & 1){
            fprintf(stderr, "expected:\n    ");
            for(size_t i = 0; i < nexp; i++){
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%s", exp[i]);
            }
            fprintf(stderr, "\nbut got:\n    ");
            for(size_t i = 0; i < matches_out->len; i++){
                match_t match = matches_out->items[i];
                char buf[1024];
                sprint_pattern(buf, *match.pattern, match.matched);
                if(i) fprintf(stderr, ", ");
                fprintf(stderr, "%s", buf);
            }
            fprintf(stderr, "\n");
        }
        if(failures & 2){
            fprintf(
                stderr,
                "expected isterminal=%s but got %s\n",
                expterminal ? "true" : "false",
                isterminal ? "true" : "false"
            );
        }
    }

    match_array_put(ma, matches_out);
fail_parse_pattern:
    for(size_t i = 0; i < nin; i++){
        pattern_free(&patterns[i]);
    }
    free(patterns);
    return !ok;
}

int test_matches_init(){
    int retval = 0;

    pool_t *p = NULL;
    match_array_t *ma = NULL;

    // the vararg is PATTERN_IN... NULL [PATTERN_OUT...]
    #define TEST_CASE(START, TERMINAL, ...) do { \
        char *strs[] = {__VA_ARGS__}; \
        size_t nstrs = sizeof(strs)/sizeof(*strs); \
        /* find the NULL string */ \
        size_t nullpos = (size_t)-1; \
        for(size_t i = 0; i < nstrs; i++){ \
            if(strs[i] == NULL){ \
                nullpos = i; \
                break; \
            } \
        } \
        if(nullpos == (size_t)-1){ \
            fprintf(stderr, "no NULL divider in input strings!\n"); \
            goto cu; \
        } \
        char **in = strs; \
        size_t nin = nullpos; \
        char **exp = &strs[nullpos + 1]; \
        size_t nexp = nstrs - nullpos - 1; \
        string_t start = S(START); \
        int ret = matches_init_test_case( \
            &p, &ma, in, nin, start, exp, nexp, TERMINAL \
        ); \
        if(ret) retval = ret; \
    } while(0)

    TEST_CASE("/1", false, "/1/**/a", "/1/**/b", "/2/**/c", "/2/**/d",
                    NULL, "**/a", "**/b");
    TEST_CASE("/2", false, "/1/**/a", "/1/**/b", "/2/**/c", "/2/**/d",
                    NULL, "**/c", "**/d");

    TEST_CASE("/", false, "**/a", "**/b", "/**/c", "/**/d", "!**/x", "!/**/y",
                   NULL, "!pwd/**/x", "!**/y", "pwd/**/a", "pwd/**/b",
                         "**/c", "**/d");

    TEST_CASE("/a/b/c", true, "/a/b/c/**", NULL, "**");

    TEST_CASE("/a/b", true, "/a/b/**", "/a/b/c/**", NULL, "**", "c/**");

    TEST_CASE("/pwd", true, "/pwd/**", ":!f:/pwd/**", NULL, "!**", "**");
    TEST_CASE("/pwd", true, "/pwd/**", ":!d:/pwd/*/**",
                        NULL, "!*/**", "**");
    TEST_CASE("/pwd", false, ":f:/pwd/**", NULL, "**");

cu:
    match_array_free(&ma);
    pool_free(&p);
    return retval;
    #undef TEST_CASE
}

int main_test_case(char *name, int exp, char *experr, int argc, char **argv){
    int retval = 0;
    fake_io_t fake_stderr;
    fake_io(&fake_stderr, stderr, "test_stderr");
    int got = findglob_main(argc, argv);
    string_t err = restore_io(&fake_stderr);
    if(got != exp){
        fprintf(
            stderr,
            "test main case '%s' failed, expected %d but got %d\n",
            name,
            exp,
            got
        );
        retval = 1;
    }
    if(!string_eq(err, S(experr))){
        fprintf(
            stderr,
            "test main case '%s' failed, expected stderr:\n%sbut got\n%.*s",
            name,
            experr,
            F(err)
        );
        retval = 1;
    }
    free(err.text);
    return retval;
}

int test_main(){
    int retval = 0;
    #define TEST_CASE(NAME, EXP, ERR, ...) do { \
        char *argv[] = {"findglob", __VA_ARGS__}; \
        int argc = sizeof(argv)/sizeof(*argv); \
        int ret = main_test_case(NAME, EXP, ERR, argc, argv); \
        if(ret) retval = ret; \
    } while(0)
    TEST_CASE(
        "only antipatterns", 1,
        "error: you provided 2 antipatterns but no patterns at all\n",
        "!a", "!**"
    );
    TEST_CASE(
        "double glob", 1,
        "a pattern cannot have two consecutive '**' elements\n",
        "**/**"
    );
    return retval;
    #undef TEST_CASE
}

int e2e_test_case(char *cwd, char *dir, char *exp, int argc, char **argv){
    int failures = 0; // 1 = exit code, 2 = text

    fake_io_t fake_stdout;
    fake_io(&fake_stdout, stdout, "test_stdout");
    if(dir){
        int ret = chdir(dir);
        if(ret){
            perror(dir);
            exit(20);
        }
    }
    int got = findglob_main((int)argc, argv);
    if(dir){
        int ret = chdir(cwd);
        if(ret){
            perror(cwd);
            exit(21);
        }
    }
    string_t out = restore_io(&fake_stdout);

    if(got) failures |= 1;
    if(!string_eq(out, S(exp))) failures |= 2;

    if(failures){
        fprintf(stderr, "e2e test case failed:");
        for(int i = 0; i < argc; i++){
            fprintf(stderr, " %s", argv[i]);
        }
        fprintf(stderr, "\n");
        if(failures & 1){
            fprintf(stderr, "expected exit code 0 but got %d\n", got);
        }
        if(failures & 2){
            fprintf(stderr, "--- expected stdout:\n%s", exp);
            fprintf(stderr, "--- but got stdout:\n%.*s", F(out));
        }
    }

    free(out.text);

    return failures;
}

#ifdef _WIN32 // windows
#define mkdir(name, mode) _mkdir(name)
int rmdir(const char *name){
    return _rmdir(name);
}
int unlink(const char *name){
    return _unlink(name);
}
#endif

// create some directories and files we can run tests against
int prep_e2e_test(){
    int retval = 0;
    #define DETECT(code, msg) if(code){ perror(msg); retval = 1; }
    DETECT(mkdir("example", 0777), "mkdir(example)")
    DETECT(mkdir("example/b", 0777), "mkdir(example/b)")
    DETECT(mkdir("example/d", 0777), "mkdir(example/d)")
    DETECT(mkdir("example/d/a", 0777), "mkdir(example/d/a)")
    DETECT(mkdir("example/d/a/c", 0777), "mkdir(example/d/a/c)")
    DETECT(mkdir("example/d/e", 0777), "mkdir(example/d/e)")
    FILE *f;
    f = fopen("example/a", "w"); DETECT(!f, "example/a") else fclose(f);
    f = fopen("example/d/f", "w"); DETECT(!f, "example/d/f") else fclose(f);
    #undef DETECT
    if(retval){
        fprintf(stderr, "prep_e2e_test failed!\n");
    }
    return retval;
}

void cleanup_e2e_test(){
    #define DETECT(code, msg) if(code){ perror(msg); }
    DETECT(unlink("example/a"), "rmdir(example/a)")
    DETECT(unlink("example/d/f"), "rmdir(example/d/f)")
    DETECT(rmdir("example/d/e"), "rmdir(example/d/e)")
    DETECT(rmdir("example/d/a/c"), "rmdir(example/d/a/c)")
    DETECT(rmdir("example/d/a"), "rmdir(example/d/a)")
    DETECT(rmdir("example/d"), "rmdir(example/d)")
    DETECT(rmdir("example/b"), "rmdir(example/b)")
    DETECT(rmdir("example"), "rmdir(example)")
    #undef DETECT
}

int test_e2e(){
    int retval = prep_e2e_test();;

    char cwd[PATH_MAX];
    char *cret = getcwd(cwd, sizeof(cwd));
    if(!cret){
        perror("getcwd");
        return 1;
    }

    // last value is the expected output
    #define TEST_CASE(DIR, ...) do { \
        char *argv[] = {"findglob", __VA_ARGS__}; \
        size_t argc = sizeof(argv)/sizeof(*argv); \
        int ret = e2e_test_case(cwd, DIR, argv[argc-1], (int)argc-1, argv); \
        if(ret) retval = ret; \
    } while(0)


    // list example tree
    TEST_CASE(NULL, "example/**",
        "example\n"
        "example/a\n"
        "example/b\n"
        "example/d\n"
        "example/d/a\n"
        "example/d/a/c\n"
        "example/d/e\n"
        "example/d/f\n"
    );

    // list example tree as .
    TEST_CASE("example", "**",
        ".\n"
        "a\n"
        "b\n"
        "d\n"
        "d/a\n"
        "d/a/c\n"
        "d/e\n"
        "d/f\n"
    );

    // XXX: enable this test on windows
    #ifndef _WIN32
    // a / root causes absolute filepaths
    TEST_CASE(NULL, "/*highly_unlikely_name*", "example/**",
        CWD "example\n"
        CWD "example/a\n"
        CWD "example/b\n"
        CWD "example/d\n"
        CWD "example/d/a\n"
        CWD "example/d/a/c\n"
        CWD "example/d/e\n"
        CWD "example/d/f\n"
    );
    #endif

    // avoid printing directories (a rather silly example)
    TEST_CASE("example", "**", ":!d:/**", "");

    // avoid printing directories (still misses the . this way)
    TEST_CASE("example", "**", ":!d:*/**", ".\na\n");
    TEST_CASE("example", "**", "!*/", ".\na\n");

    // print only files
    TEST_CASE("example", ":f:**", "a\nd/f\n");

    // avoid printing files
    TEST_CASE("example", "**", ":!f:**",
        ".\n"
        "b\n"
        "d\n"
        "d/a\n"
        "d/a/c\n"
        "d/e\n"
    );

    // print only directories
    TEST_CASE("example", ":d:**",
        ".\n"
        "b\n"
        "d\n"
        "d/a\n"
        "d/a/c\n"
        "d/e\n"
    );

    // search two peer directories
    TEST_CASE("example", "b/**", "d/**",
        "b\n"
        "d\n"
        "d/a\n"
        "d/a/c\n"
        "d/e\n"
        "d/f\n"
    );

    // match explicitly named files
    TEST_CASE("example", "a", "a\n");
    TEST_CASE("example", "a/", "");
    TEST_CASE("example", "a", "!a/", "a\n");
    TEST_CASE("example", "a", ":!f:a", "");

    // match explicitly named directories
    TEST_CASE(NULL, "example", "example\n");
    TEST_CASE(NULL, "example/", "example\n");
    TEST_CASE(NULL, "example", "!example/", "");
    TEST_CASE(NULL, "example", ":!f:example", "example\n");

    cleanup_e2e_test();

    return retval;
    #undef TEST_CASE
}

int main(){
    int retval = 0;
    #define RUN_TEST(fn) do { \
        int ret = fn(); \
        if(ret) retval = ret; \
    } while(0)
    RUN_TEST(test_string);
    RUN_TEST(test_glob_match);
    RUN_TEST(test_path_iter);
    RUN_TEST(test_roots_iter);
    RUN_TEST(test_section_parse);
    RUN_TEST(test_pattern_parse);
    RUN_TEST(test_pattern_rewrite_start);
    RUN_TEST(test_match_text);
    RUN_TEST(test_process_dir);
    RUN_TEST(test_matches_init);
    RUN_TEST(test_main);
    RUN_TEST(test_e2e);
    fprintf(stderr, retval ? "FAIL\n" : "PASS\n");
    return retval;
}
