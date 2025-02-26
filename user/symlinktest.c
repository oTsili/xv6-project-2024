#include "kernel/param.h"
#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/riscv.h"
#include "kernel/fcntl.h"
#include "kernel/spinlock.h"
#include "kernel/sleeplock.h"
#include "kernel/fs.h"
#include "kernel/file.h"
#include "user/user.h"

#define fail(msg) do {printf("FAILURE: " msg "\n"); failed = 1; goto done;} while (0);
static int failed = 0;

static void testsymlink(void);
static void concur(void);
static void cleanup(void);

int main(int argc, char *argv[]) {
  cleanup();
  testsymlink();
  concur();
  exit(failed);
}

static void cleanup(void) {
  // Clean up all symlinks and files created during the test
  unlink("/testsymlink/a");
  unlink("/testsymlink/b");
  unlink("/testsymlink/c");
  unlink("/testsymlink/1");
  unlink("/testsymlink/2");
  unlink("/testsymlink/3");
  unlink("/testsymlink/4");
  unlink("/testsymlink/z");
  unlink("/testsymlink/y");
  unlink("/testsymlink");
}

// stat a symbolic link using O_NOFOLLOW
static int stat_slink(char *pn, struct stat *st) {
  int fd = open(pn, O_RDONLY | O_NOFOLLOW);
  if(fd < 0)
    return -1;
  if(fstat(fd, st) != 0)
    return -1;
  return 0;
}

static void testsymlink(void) {
  int r, fd1 = -1, fd2 = -1;
  char buf[4] = {'a', 'b', 'c', 'd'};
  char c = 0, c2 = 0;
  struct stat st;

  printf("Start: test symlinks\n");

  // Create test directory for symlinks
  mkdir("/testsymlink");

  // Open a file for writing
  fd1 = open("/testsymlink/a", O_CREATE | O_RDWR);
  if(fd1 < 0) fail("failed to open a");

  // Create symlink b -> a
  r = symlink("/testsymlink/a", "/testsymlink/b");
  if(r < 0)
    fail("symlink b -> a failed");

  // Write data to 'a'
  if(write(fd1, buf, sizeof(buf)) != 4)
    fail("failed to write to a");

  // Stat symlink b
  if (stat_slink("/testsymlink/b", &st) != 0)
    fail("failed to stat b");
  if(st.type != T_SYMLINK)
    fail("b isn't a symlink");

  // Open symlink b and read data
  fd2 = open("/testsymlink/b", O_RDWR);
  if(fd2 < 0)
    fail("failed to open b");
  read(fd2, &c, 1);
  if (c != 'a')
    fail("failed to read bytes from b");

  // Unlink 'a' and check if 'b' is still accessible
  unlink("/testsymlink/a");
  if(open("/testsymlink/b", O_RDWR) >= 0)
    fail("Should not be able to open b after deleting a");

  // Create a symlink cycle: a -> b -> a
  r = symlink("/testsymlink/b", "/testsymlink/a");
  if(r < 0)
    fail("symlink a -> b failed");

  r = open("/testsymlink/b", O_RDWR);
  if(r >= 0)
    fail("Should not be able to open b (cycle a -> b -> a)");

  // Test symlink to nonexistent file (should fail)
  // r = symlink("/testsymlink/nonexistent", "/testsymlink/c");
  // if(r == 0)
  //   fail("Symlinking to nonexistent file should fail");

  // Chain of symlinks
  r = symlink("/testsymlink/2", "/testsymlink/1");
  if(r) fail("Failed to link 1 -> 2");
  r = symlink("/testsymlink/3", "/testsymlink/2");
  if(r) fail("Failed to link 2 -> 3");
  r = symlink("/testsymlink/4", "/testsymlink/3");
  if(r) fail("Failed to link 3 -> 4");

  // Cleanup file descriptors
  close(fd1);
  close(fd2);

  // Reopen file descriptors for testing chained symlinks
  fd1 = open("/testsymlink/4", O_CREATE | O_RDWR);
  if(fd1 < 0) fail("Failed to create 4");
  fd2 = open("/testsymlink/1", O_RDWR);
  if(fd2 < 0) fail("Failed to open 1");

  // Write to 1 and read from 4
  c = '#';
  r = write(fd2, &c, 1);
  if(r != 1) fail("Failed to write to 1");
  r = read(fd1, &c2, 1);
  if(r != 1) fail("Failed to read from 4");
  if(c != c2)
    fail("Value read from 4 differed from value written to 1");

  printf("test symlinks: ok\n");

done:
  close(fd1);
  close(fd2);
}

static void concur(void) {
  int pid, i;
  int fd;
  struct stat st;
  int nchild = 2;

  printf("Start: test concurrent symlinks\n");

  // Create an initial file
  fd = open("/testsymlink/z", O_CREATE | O_RDWR);
  if(fd < 0) {
    printf("FAILED: open failed\n");
    exit(1);
  }
  close(fd);

  // Fork child processes to create symlinks concurrently
  for(int j = 0; j < nchild; j++) {
    pid = fork();
    if(pid < 0){
      printf("FAILED: fork failed\n");
      exit(1);
    }
    if(pid == 0) {
      int m = 0;
      unsigned int x = (pid ? 1 : 97);
      for(i = 0; i < 100; i++){
        x = x * 1103515245 + 12345;
        if((x % 3) == 0) {
          symlink("/testsymlink/z", "/testsymlink/y");
          if (stat_slink("/testsymlink/y", &st) == 0) {
            m++;
            if(st.type != T_SYMLINK) {
              printf("FAILED: not a symbolic link\n");
              exit(1);
            }
          }
        } else {
          unlink("/testsymlink/y");
        }
      }
      exit(0);
    }
  }

  int r;
  for(int j = 0; j < nchild; j++) {
    wait(&r);
    if(r != 0) {
      printf("test concurrent symlinks: failed\n");
      exit(1);
    }
  }

  printf("test concurrent symlinks: ok\n");
}