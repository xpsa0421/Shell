CC=gcc
EXE=tloop segfault forever shell

all: $(EXE)

tloop: tloop.c
	$(CC) $< -o $@

forever: forever.c
	$(CC) $< -o $@

segfault: segfault.c
	$(CC) $< -o $@

shell: shell.c
	$(CC) $< -o $@

clean:
	rm $(EXE)
