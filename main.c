#include <stdio.h>
#include <string.h>
#include <libmemcached/memcached.h>

static int parse_host(char * hostport, char ** hostname, int * port) {
  char *ptr;
  *hostname = strdup(hostport);
  ptr = strchr(*hostname, ':');
  if (ptr != NULL) {
    *ptr = '\0';
    *port = atoi(ptr + 1);
  } else {
    *port = 11211;
  }

  return 0;
}

int main(int argc, char **argv) {

  char *hostname;
  int port;
  char *filename;
  bool check = false;
  
  if (argc < 3) {
    printf("mc-loader <server>:<port> <filename> [check]\n");
    exit (1);
  }

  if (argc > 3) {
    check = true;
  }

  parse_host(argv[1], &hostname, &port);
  filename = strdup(argv[2]);

  /*
  printf("host: %s\n", hostname);
  printf("port: %d\n", port);
  printf("file: %s\n", filename);
  */

  memcached_st *memc = memcached_create(NULL);
  memcached_server_add(memc, hostname, port);

  char *buffer = malloc(sizeof(char) * 64);
  char *key = NULL;
  size_t nkey = 0;
  char *data = NULL;
  size_t size = 0;
  char *rdata = NULL;
  size_t rsize = 0;
  char *ptr = NULL;
  uint32_t flags;
  memcached_return_t rc;
  bool pass;
  int rval = 0;

  FILE *file = fopen(filename, "r");
  while (fgets(buffer,63,file) != NULL) {
    key = buffer;
    ptr = strchr(key,' ');
    if (ptr == NULL) {
      continue;
    }
    *ptr = '\0';
    data = ptr + 1;
    ptr = strchr(data,'\n');
    if (ptr != NULL) {
      *ptr = '\0';
    }
    nkey = strlen(key);
    size = strlen(data);

    if (check == false) {
      memcached_set(memc, key, nkey, data, size, 0, 0);
      if (rc != MEMCACHED_SUCCESS) {
	rval = 1;
	printf("Failed to set: %s\n", key);
      }
    } else {
      rdata = memcached_get(memc, key, nkey, &rsize, &flags, &rc);
      pass = true;
      if (rc != MEMCACHED_SUCCESS) {
	pass = false;
      } else if (rsize != size) {
	pass = false;
      } else if (memcmp(data, rdata, size) != 0) {
	pass = false;
      }
      if (pass == false) {
	rval = 1;
	printf("Failed to get: %s\n", key);
      }
    }
  }
  return rval;
}
