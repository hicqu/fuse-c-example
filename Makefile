fs: main.c
	clang -O2 -Wall -D_FILE_OFFSET_BITS=64 -lfuse $^ -o $@ 

clean:
	rm -rf fs

format: main.c
	clang-format -sort-includes -i $^
