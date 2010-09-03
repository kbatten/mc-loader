all: main.c
	gcc -o mc-loader main.c -Wl,-rpath,/usr/local/lib -lmemcached

debug: main.c
	gcc -o mc-loader main.c -Wl,-rpath,/usr/local/lib -lmemcached -O0 -gfull

clean:
	rm -f mc-loader
	rm -rf mc-loader.dSYM
