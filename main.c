#include "sqlite3.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char * argv0;
static sqlite3 * db;

static int usage() {
  fprintf(stderr, "Usage:\n\n");
  fprintf(stderr, "    %s <command> <args...>\n\n", argv0);
  fprintf(stderr, "Where command can be:\n\n");
  fprintf(stderr, "    reset - creates a new DB, destroying existing one\n\n");
  return 1;
}

static char * slurp(const char * file) {
  FILE * f = fopen(file, "rb");
  assert(f);

  assert(0 == fseek(f, 0, SEEK_END));
  long sz = ftell(f);
  assert(sz);
  assert(0 == fseek(f, 0, SEEK_SET));

  char * data = malloc(sz + 1);
  assert(1 == fread(data, sz, 1, f));
  data[sz] = 0;

  fclose(f);
  return data;
}

static int sql_check(int rc, const char * msg) {
  if (rc == SQLITE_OK) return 0;
  fprintf(stderr, "%s: %s\n%s", msg, sqlite3_errstr(rc), sqlite3_errmsg(db));
  return 1;
}
#define _(x) if (sql_check((x), #x)) return 1;

int run_reset(int argc, char ** argv) {
  if (argc != 0) return usage();

  char * sql = slurp("main.sql");

  char * err = NULL;
  _(sqlite3_exec(db, sql, NULL, NULL, &err));

  return 0;
}

int run(int argc, char ** argv) {
  if (argc == 0) return usage();

  if (0 == strcmp(*argv, "reset")) return run_reset(--argc, ++argv);
  else return usage();
}

int main(int argc, char ** argv) {
  const char * home = getenv("HOME");
  assert(home && "missing HOME environment");

  argv0 = argv[0];

  char dbf[1024];
  snprintf(dbf, 1023, "%s/.java-classdb.sqlite", home);

  _(sqlite3_open(dbf, &db));

  int res = run(--argc, ++argv);

  sqlite3_close(db);

  return res;
}
