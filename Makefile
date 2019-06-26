fs: main.c map.rs
	rustc --crate-type=staticlib -o libmap.a map.rs
	gcc -c -O2 -Wall -D_FILE_OFFSET_BITS=64 main.c -o main.o
	gcc main.o libmap.a -lfuse -lpthread -ldl -o $@

clean:
	rm -rf fs

format: main.c map.rs
	clang-format -sort-includes -i main.c
	rustfmt map.rs
