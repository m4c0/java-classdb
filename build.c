#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int run(char ** args) {
  assert(args && args[0]);

  pid_t pid = fork();
  if (pid == 0) {
    execvp(args[0], args);
    abort();
  } else if (pid > 0) {
    int sl = 0;
    assert(0 <= waitpid(pid, &sl, 0));
    if (WIFEXITED(sl)) return WEXITSTATUS(sl);
  }

  fprintf(stderr, "failed to run child process: %s\n", args[0]);
  return 1;
}
#define RUN(...) do { char * argv[] = { __VA_ARGS__, 0 }; if (run(argv)) return 1; } while (0)

int main() {
  if (!fopen("sqlite3.o", "rb")) RUN("clang", "-c", "-O3", "-o", "sqlite3.o", "sqlite3.c");
  RUN("clang", "-Wall", "-c", "-g", "-o", "main.o", "main.c");
  RUN("clang", "-g",  "-o", "classdb", "main.o", "sqlite3.o");
  return 0;
}
