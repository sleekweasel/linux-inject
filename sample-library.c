#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>


/* Simplest thing would be to set SIGCHLD handler, but that's intrusive.
 *
 * Instead, wait every few seconds for dead children over 120 seconds old, called adb or whatever.
 *
 * Could wait only for old zombies we also saw in the last sweep, to avoid races.
 */

#define OLD_AGE 180

void* doSomeThing(void *arg)
{
  printf("\n Injected thread started\n");
  char ps[256];
  sprintf(ps, "ps --sort pid --width=80 --ppid %ji ostate=,pid=,etimes=,command=",
    (intmax_t) getpid());
  printf("\n Injected thread will test %s\n", ps);

  pid_t* plist[2];
  int list = 0;
  plist[0] = calloc(8, sizeof(pid_t)); *plist[0] = 8; // And list is 0'd
  plist[1] = calloc(8, sizeof(pid_t)); *plist[1] = 8; // And list is 0'd

  while (1) {
    for (
        int to_sleep = 20;
        to_sleep > 0;
        to_sleep = sleep(to_sleep) - 1) {
      ; // Avoid pathological sleep with -1
    }

    FILE *fp = popen(ps, "r");
    if (fp == NULL) {
      printf("\n Injected thread popen failed. Try later.\n", ps);
      break;
    }

    char status;
    intmax_t pid;
    intmax_t etimes;
    char* cmd;
    int cols;
    int row = 1; // Skip [0]=size of vector; 0-terminated
    // cat /proc/44837/stat
    //  pid   exe    ppid
    //  %d   %s    %c %d
    // 44837 (adb) Z 939 1 1 0 -1
    //  ps axostate,pid,etimes,comm|grep Z
    //  Z   269  191632 adb <defunct>
    // 47965 ?        Z      0:00                      \_ [adb] <defunct>
    // 50000 ?        Z      0:00                      \_ [qemu-system-x86] <defunct>
    list = 1 - list;
    while ((cols = fscanf(fp, "%c %ji %ji %ms", &status, &pid, &etimes, &cmd)) != EOF) {
        if (cols == 4) {
          if (status == 'Z' && etimes > OLD_AGE && (
                strcmp(cmd, "adb") == 0 ||
                strcmp(cmd,"qemu-system-x86") ||
                strcmp(cmd, "[adb]") == 0 ||
                strcmp(cmd,"[qemu-system-x86]") ||
            )) {
              plist[list][row++] = pid;
              if (plist[list][0] == row) {
                plist[list][0] *= 2;
                plist[list] = realloc(plist[list], sizeof(pid_t) * plist[list][0]);
              }
          }
          free(cmd);
        }
        while ( (status = fgetc(fp)) != EOF && status != 10 ) {
          ;  // Eat any trailing line.
        }
    }
    plist[list][row] = 0;
    pclose(fp);
    // Wait for persistent zombies. Zigzag stepping on both lists.
    char found = 0;
    for (
        int i = 1, j = 1;
        plist[0][i] != 0 && plist[1][j] != 0; // No need to exhaust both lists
        plist[0][i] < plist[1][j] ? ++i : ++j ) {  // Harmless to step only one or the other list
      if ( plist[0][i] == plist[1][j] ) {
          waitpid(plist[0][i], NULL, WNOHANG); // Wait on a common zombie.
          printf("w-%d ", plist[0][i]);
          found = 1;
      }
    }
    if (found) { printf("\n"); }
  } // Forever.
  return NULL;
}


/*
 * hello()
 *
 * Hello world function exported by the sample library.
 *
 */

void hello()
{
	printf("I just got loaded\n");

  pthread_t tid;
  int err = pthread_create(&tid, NULL, &doSomeThing, NULL);
  if (err != 0)
    printf("\ncan't create thread :[%s]\n", strerror(err));
  else
    printf("\n Thread created successfully\n");
}

/*
 * loadMsg()
 *
 * This function is automatically called when the sample library is injected
 * into a process. It calls hello() to output a message indicating that the
 * library has been loaded.
 *
 */

__attribute__((constructor))
void loadMsg()
{
	hello();
}
