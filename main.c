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
  fprintf(stderr, "    add_jar <jar> - loads class from a jar into the DB\n\n");
  fprintf(stderr, "    find <query> - finds a class by name\n\n");
  fprintf(stderr, "    javap <class> - given a class name, find a URL suitable for javap\n\n");
  fprintf(stderr, "    reset - creates a new DB, destroying existing one\n\n");
  fprintf(stderr, "If you are a VIM user, these might be useful:\n\n");
  fprintf(stderr, "    ctags - creates a ctags file with the entire DB\n\n");
  fprintf(stderr, "    tagfunc <tag> - outputs ctags for a given tag\n\n");
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
  fprintf(stderr, "%s: %s\n%s\n", msg, sqlite3_errstr(rc), sqlite3_errmsg(db));
  return 1;
}
#define _chk(x, exp) if (sql_check((x), exp, #x)) return 1;
#define _(x) _chk(x, SQLITE_OK)

typedef struct zip_hdr_s {
  uint16_t version;
  uint16_t flag;
  uint16_t method;
  uint16_t mod_time;
  uint16_t mod_date;
  uint32_t crc32;
  uint32_t comp_size;
  uint32_t uncomp_size;
  uint16_t fname_len;
  uint16_t extra_len;
} __attribute__((packed)) zip_hdr_t;
int run_add_jar(int argc, char ** argv) {
  if (argc != 1) return usage();

  FILE * zip = fopen(*argv, "rb");
  assert(zip);

  char * err = NULL;
  _(sqlite3_exec(db, "BEGIN TRANSACTION", NULL, NULL, &err));

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "INSERT OR REPLACE INTO class (jar, fqn, name) VALUES (?, ?, ?)", -1,
        &stmt, NULL));

  char buf[65536];
  zip_hdr_t hdr;
  int count = 0;
  while (!feof(zip)) {
    uint32_t magic;
    int n = fread(&magic, sizeof(magic), 1, zip);
    if (n <= 0) break;
    if (magic != 0x04034b50) {
      fseek(zip, -3, SEEK_CUR);
      continue;
    }

    assert(fread(&hdr, sizeof(hdr), 1, zip));
    assert(fread(buf, hdr.fname_len, 1, zip));
    buf[hdr.fname_len] = 0;

    if (0 == strncmp("META-INF/", buf, 9)) continue;

    char * ext = strrchr(buf, '.');
    if (!ext) continue;
    if (strcmp(ext, ".class")) continue;

    *ext = 0;
    char * name = strrchr(buf, '$');
    if (!name) name = strrchr(buf, '/');
    name = name ? name + 1 : buf;
    if (0 == strcmp(name, "/package-info")) continue;

    _(sqlite3_reset(stmt));
    _(sqlite3_bind_text(stmt, 1, *argv, -1, NULL));
    _(sqlite3_bind_text(stmt, 2, buf,   -1, NULL));
    _(sqlite3_bind_text(stmt, 3, name,  -1, NULL));
    _chk(sqlite3_step(stmt), SQLITE_DONE);

    count++;
  }

  fclose(zip);
  sqlite3_finalize(stmt);

  _(sqlite3_exec(db, "COMMIT", NULL, NULL, &err));

  fprintf(stderr, "loaded %d classes from %s\n", count, *argv);

  return 0;
}

int run_ctags(int argc, char ** argv) {
  if (argc != 0) return usage();

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "SELECT jar, fqn, name FROM class ORDER BY name, jar, fqn", -1,
        &stmt, NULL));

  FILE * f = fopen("tags", "w");
  assert(f);

  int rc;
  while (SQLITE_DONE != (rc = sqlite3_step(stmt))) {
    const uint8_t * jar  = sqlite3_column_text(stmt, 0);
    const uint8_t * cls  = sqlite3_column_text(stmt, 1);
    const uint8_t * name = sqlite3_column_text(stmt, 2);

    fprintf(f, "%s\tjar:file://%s!/%s.class\t/\\<%s\\>\\.\\*{$/\n", name, jar, cls, name);
  }

  fclose(f);

  return 0;
}

int run_find(int argc, char ** argv) {
  if (argc != 1) return usage();

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "SELECT fqn FROM class WHERE fqn GLOB ?", -1,
        &stmt, NULL));
  _(sqlite3_bind_text(stmt, 1, *argv, -1, NULL));

  int rc;
  while (SQLITE_DONE != (rc = sqlite3_step(stmt))) {
    printf("%s\n", sqlite3_column_text(stmt, 0));
  }
  if (sql_check(rc, SQLITE_DONE, "sqlite3_step(stmt)")) return 1;

  sqlite3_finalize(stmt);

  return 0;
}

int run_javap(int argc, char ** argv) {
  if (argc != 1) return usage();

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "SELECT jar FROM class WHERE fqn = ?", -1,
        &stmt, NULL));
  _(sqlite3_bind_text(stmt, 1, *argv, -1, NULL));

  _chk(sqlite3_step(stmt), SQLITE_ROW);
  printf("jar:file://%s!/%s.class\n", sqlite3_column_text(stmt, 0), *argv);

  _chk(sqlite3_step(stmt), SQLITE_DONE);

  return 0;
}

int run_reset(int argc, char ** argv) {
  if (argc != 0) return usage();

  char * sql = slurp("main.sql");

  char * err = NULL;
  _(sqlite3_exec(db, sql, NULL, NULL, &err));

  return 0;
}

int run_tagfunc(int argc, char ** argv) {
  if (argc != 1) return usage();

  sqlite3_stmt * stmt;
  _(sqlite3_prepare_v2(db,
        "SELECT jar, fqn, name FROM class WHERE name = ? ORDER BY name, jar, fqn", -1,
        &stmt, NULL));
  _(sqlite3_bind_text(stmt, 1, *argv, -1, NULL));

  int rc;
  while (SQLITE_DONE != (rc = sqlite3_step(stmt))) {
    const uint8_t * jar  = sqlite3_column_text(stmt, 0);
    const uint8_t * cls  = sqlite3_column_text(stmt, 1);
    const uint8_t * name = sqlite3_column_text(stmt, 2);

    printf("%s\tjar:file://%s!/%s.class\t/\\<%s\\>\\.\\*{$/\n", name, jar, cls, name);
  }

  sqlite3_finalize(stmt);

  return 0;
}

int run(int argc, char ** argv) {
  if (argc == 0) return usage();

  if (0 == strcmp(*argv, "add-jar")) return run_add_jar(--argc, ++argv);
  if (0 == strcmp(*argv, "ctags"  )) return run_ctags  (--argc, ++argv);
  if (0 == strcmp(*argv, "find"   )) return run_find   (--argc, ++argv);
  if (0 == strcmp(*argv, "javap"  )) return run_javap  (--argc, ++argv);
  if (0 == strcmp(*argv, "reset"  )) return run_reset  (--argc, ++argv);
  if (0 == strcmp(*argv, "tagfunc")) return run_tagfunc(--argc, ++argv);

  return usage();
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
