#include <stdio.h>
#include <string.h>
#include <libmemcached/memcached.h>

typedef enum {
  MCLOADER_SUCCESS = 0,
  MCLOADER_MCFAIL  = 1,
  MCLOADER_SIZEDIF = 2,
  MCLOADER_DATADIF = 3,
} MCLOADER_ERROR_CODE;

/*
  convert hostname:port to char*:int
  if port is not specified then it defaults to 11211
*/
static int parse_host(char *hostport, char **hostname, int *port) {
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

/*
  convert username:password to char*:char*
  if password is not specified it defaults to ""
*/
static int parse_auth(char *auth, char **username, char **password) {
  char *ptr;
  *username = strdup(auth);
  ptr = strchr(*username, ':');
  if (ptr != NULL) {
    *ptr = '\0';
    *password = strdup(ptr + 1);
  } else {
    *password = strdup("");
  }

  return 0;
}

int main(int argc, char **argv) {

  char *hostname;
  int port;
  char *filename;
  bool check = false;
  bool binary = false;
  bool sasl = false;
  char *sasl_username;
  char *sasl_password;
  int i;
  int j;
  memcached_st *memc;
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
  FILE *file;
  char *fixed_data = NULL;
  int fixed_datasize = 0;
  int backoff_us = 0;
  int fails = 0;
  int passes = 0;
  int oom_error_code = 10;
  int err_reason = MCLOADER_SUCCESS;

  /* parse out arguments */
  if (argc < 3) {
    printf("mc-loader <server>:<port> <keyset> [check] [binary] [valuesize size] [sasl username:password]\n");
    exit (1);
  }

  parse_host(argv[1], &hostname, &port);
  filename = strdup(argv[2]);

  for (i=3; i < argc; i++) {
    if (strncmp("check", argv[i], 6) == 0) {
      check = true;
    }
    else if (strncmp("binary", argv[i], 7) == 0) {
      binary = true;
      oom_error_code = 8;
    }
    else if (strncmp("valuesize", argv[i], 9) == 0) {
      if (argc < (i+2)) {
        fprintf(stderr, "Missing value size\n");
        exit(1);
      }
      /* right now just fill the data with 'a', in the future
         create a random (but predictable) set of data based
         on the key and data specified in the file
      */
      fixed_datasize = atoi(argv[i+1]);
      fixed_data = malloc(fixed_datasize+1);
      for (j=0;j<fixed_datasize;j++) {
        fixed_data[j]='a';
      }
      fixed_data[fixed_datasize] = 0;
      i = i + 1;
    }
    else if (strncmp("sasl", argv[i], 4) == 0) {
      if (argc < (i+2)) {
        fprintf(stderr, "Missing SASL username:password\n");
        exit(1);
      }
      sasl = true;
      binary = true;
      parse_auth(argv[i+1], &sasl_username, &sasl_password);
      i = i + 1;
    }
  }

  /* connect to the memcached server */
  memc = memcached_create(NULL);
  memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_NO_BLOCK, 1);
  if (binary) {
    memcached_behavior_set(memc, MEMCACHED_BEHAVIOR_BINARY_PROTOCOL, 1);
  }
  if (sasl) {
    if (sasl_client_init(NULL) != SASL_OK) {
      fprintf(stderr, "Failed to initialize sasl library!\n");
      exit(1);
    }

    memc->sasl = malloc(sizeof(struct memcached_sasl_st));
    memc->sasl->callbacks = NULL;
    memc->sasl->is_allocated = false;
    memcached_set_sasl_auth_data(memc, sasl_username, sasl_password);
  }
  memcached_server_add(memc, hostname, port);

  if (strcmp(filename,"-") == 0) {
    file = stdin;
  } else {
    file = fopen(filename, "r");
  }
  if (file == NULL) {
    fprintf(stderr, "Failed to open file %s\n", filename);
    exit(1);
  }

  /* read in keys and either set or get */
  while (fgets(buffer,63,file) != NULL) {
    key = buffer;
    ptr = strchr(key, ' ');
    if (ptr == NULL) {
      continue;
    }
    *ptr = '\0';
    data = ptr + 1;
    ptr = strchr(data, '\n');
    if (ptr != NULL) {
      *ptr = '\0';
    }
    if (fixed_data != NULL) {
      data = fixed_data;
    }
    nkey = strlen(key);
    size = strlen(data);

    if (check == false) {
      /* if we fail to set, backoff then try again up to a 10 second backoff */
      do {
        rc = memcached_set(memc, key, nkey, data, size, 0, 0);
        /* only backoff on temp mem errors */
        if (rc == oom_error_code) {
          backoff_us += 10000 + (backoff_us/20);
          if (backoff_us > 4000000) {
            backoff_us = 4000000;
          }
#ifdef VERBOSE
          fprintf(stderr, "backing off %s, %d us due to error: %d\n", key, backoff_us, rc);
#endif
          usleep(backoff_us);
        }
      } while ((rc == oom_error_code) && (backoff_us < 4000000));
      if (rc != MEMCACHED_SUCCESS) {
        rval = 1;
        fails ++;
        fprintf(stderr, "Failed to set: %s, due to error: %d\n", key, rc);
      } else {
        passes ++;
      }
      backoff_us -= (10000 + (backoff_us/20));
      if (backoff_us < 0) {
        backoff_us = 0;
      }
#ifdef VERBOSE
      if (backoff_us > 0) {
        fprintf(stderr, "backoff: %d us\n", backoff_us);
      }
#endif
    } else {
      rdata = memcached_get(memc, key, nkey, &rsize, &flags, &rc);
      err_reason = MCLOADER_SUCCESS;
      pass = true;
      if (rc != MEMCACHED_SUCCESS) {
        pass = false;
        err_reason = MCLOADER_MCFAIL;
      } else if (rsize != size) {
        pass = false;
        err_reason = MCLOADER_SIZEDIF;
      } else if (memcmp(data, rdata, size) != 0) {
        pass = false;
        err_reason = MCLOADER_DATADIF;
      }
      if (pass == false) {
        rval = 1;
        fails ++;
        switch (err_reason) {
        case MCLOADER_MCFAIL:
          fprintf(stderr, "Failed to get: %s, memcached failure %d\n", key, rc);
          break;
        case MCLOADER_SIZEDIF:
            fprintf(stderr, "Failed to get: %s, data size difference. expected %lu, got %lu\n", key, size, rsize);
          break;
        case MCLOADER_DATADIF:
          fprintf(stderr, "Failed to get: %s, data value difference\n", key);
          break;
        default:
          fprintf(stderr, "Failed to get: %s, mcloader failure %d\n", key, err_reason);
        }
      } else {
        passes ++;
      }
      free(rdata);
    }
  }

  if (sasl) {
    memcached_destroy_sasl_auth_data(memc);
  }

  printf("pass: %d\n",passes);
  printf("fail: %d\n",fails);

  return rval;
}
