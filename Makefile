all: main.c
	gcc -o mc-loader main.c -lmemcached -Wl,-rpath,/usr/local/lib

clean:
	rm -f mc-loader
