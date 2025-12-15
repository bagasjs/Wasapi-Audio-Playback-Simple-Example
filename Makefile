CC := clang
CFLAGS := -Wall -Wextra -pedantic -g -fsanitize=address -D_CRT_SECURE_NO_WARNINGS 
LFLAGS := -lole32

build/main.exe: main.c
	$(CC) $(CFLAGS) -o $@ $^ $(LFLAGS)
