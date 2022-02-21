#include <stdio.h>

int main(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "usage: %s %%cd%%\n", argv[0]);
        return 1;
    }
    // only print after the initial C: drive
    int have_sep = 0;
    for(char *p = argv[1]; *p; p++){
        if(*p == '\\'){
            fputc('/', stdout);
            have_sep = 1;
        }else if(have_sep){
            fputc(*p, stdout);
        }
    }
    return 0;
}
