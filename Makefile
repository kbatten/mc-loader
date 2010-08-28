all: main.c
	gcc -o mc-loader main.c -lmemcached

clean:
	rm -f mc-loader
