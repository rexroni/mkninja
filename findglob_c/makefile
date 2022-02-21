all: findglob test

findglob: makefile findglob.c main.c
	gcc -Wall -Wextra -Werror main.c -o findglob -O3

test: makefile findglob.c test.c
	gcc -Wall -Wextra -Werror test.c -o test -g -DCWD=\"$(PWD)/\"

clean:
	rm -f test findglob
