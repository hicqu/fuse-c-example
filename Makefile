fs-debug: main.c map.rs
	rustc --crate-type=staticlib -g -o libmap.a map.rs
	gcc -c -ggdb3 -Wall -D_FILE_OFFSET_BITS=64 main.c -o main.o
	gcc main.o libmap.a -lfuse -lpthread -ldl -o $@
	@rm -rf main.o libmap.a

fs: main.c map.rs
	rustc --crate-type=staticlib -O -o libmap.a map.rs
	gcc -c -O2 -Wall -D_FILE_OFFSET_BITS=64 main.c -o main.o
	gcc main.o libmap.a -lfuse -lpthread -ldl -o $@
	@rm -rf main.o libmap.a

clean:
	rm -rf fs fs-debug main.o

format: main.c map.rs
	clang-format -sort-includes -i main.c
	rustfmt map.rs
