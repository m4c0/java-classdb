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
  fprintf(stderr, "    add_jar - loads class from a jar into the DB\n\n");
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

static int sql_check(int rc, int exp, const char * msg) {
  if (rc == exp) return 0;
  fprintf(stderr, "%s: %s\n%s", msg, sqlite3_errstr(rc), sqlite3_errmsg(db));
  return 1;
}
#define _chk(x, exp) if (sql_check((x), exp, #x)) return 1;
#define _(x) _chk(x, SQLITE_OK)

int run_reset(int argc, char ** argv) {
  if (argc != 0) return usage();

  char * sql = slurp("main.sql");

  char * err = NULL;
  _(sqlite3_exec(db, sql, NULL, NULL, &err));

  return 0;
}

int run_add_jar(int argc, char ** argv) {
  if (argc == 0) return usage();

  char buf[1024];
  snprintf(buf, 1024, "jar tf '%s'", *argv);
  FILE * f = popen(buf, "r");
  assert(f);

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "INSERT INTO class (jar, name, fqn) VALUES (?, ?, ?)", -1,
        &stmt, NULL));

  while (fgets(buf, 1024, f)) {
    buf[strlen(buf) - 1] = 0; // Chop EOL

    char * ext = strrchr(buf, '.');
    if (!ext) continue;
    if (strcmp(ext, ".class")) continue;

    *ext = 0;
    char * name = strrchr(buf, '/');
    name = name ? name + 1 : buf;
    if (0 == strcmp(name, "/package-info")) continue;

    _(sqlite3_reset(stmt));
    _(sqlite3_bind_text(stmt, 1, *argv, -1, NULL));
    _(sqlite3_bind_text(stmt, 2, name,  -1, NULL));
    _(sqlite3_bind_text(stmt, 3, buf,   -1, NULL));
    _chk(sqlite3_step(stmt), SQLITE_DONE);
  }

  pclose(f);

  sqlite3_finalize(stmt);

  return 0;
}

int run(int argc, char ** argv) {
  if (argc == 0) return usage();

  if      (0 == strcmp(*argv, "reset"  )) return run_reset  (--argc, ++argv);
  else if (0 == strcmp(*argv, "add_jar")) return run_add_jar(--argc, ++argv);
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
