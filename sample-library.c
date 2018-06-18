#include <dirent.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

char buf[256];

pid_t ppid_of_zombie(const char *pid_s) {
  if ('0' > *pid_s || *pid_s > '9') { return 0; }          // pids only, thanks.
  sprintf(buf, "/proc/%s/stat", pid_s);
  FILE *fp = fopen(buf, "r");
  if (fp == NULL) {
    perror(buf); return 0;
  }
  size_t ct = fread(buf, 1, 200, fp);
  buf[ct] = 0;
  fclose(fp);
  // cat /proc/44837/stat
  // pid   exe   st ppid ...
  // %d    %s    %c %d ...
  // 44837 (adb) Z 939 1 1 0 -1
  char *p = strchr(buf, ' ');     // Space after pid
  if (!*p++) { return 0; }        // Space, not nul.
  p = strchr(p, ' ');             // Space after exe
  if (!*p++) { return 0; }        // Space, not nul.
  if (*p != 'Z') { return 0; }    // Zombies only past this point
  p = strchr(p, ' ');             // Space after zombie
  if (!*p++) { return 0; }        // Space, not nul.
  return atol(p);
}


int pid_cmp (const void *a, const void *b) { return ( *(pid_t*)a - *(pid_t*)b ); }


/* Simplest thing would be to set SIGCHLD handler, but that's intrusive.
 *
 * Scan /proc/.../stat for zombies we also saw in the last sweep.
 *
 * Avoid forking ps, due to process table ugh.
 */

void *doSomeThing(void *arg)
{
  pid_t ppid = getpid();
  printf("\n Injected thread started for pid=%d\n", ppid);

  pid_t* plist[2];
  plist[0] = calloc(8, sizeof(pid_t)); *plist[0] = 8; // And list is 0'd
  plist[1] = calloc(8, sizeof(pid_t)); *plist[1] = 8; // And list is 0'd
  int list = 0;

  int to_sleep = 0;
  while (1) {
    while (to_sleep > 0) {
      to_sleep = sleep(to_sleep) - 1; // Avoid pathological sleep with -1
    }
    to_sleep = 20; // For next time.

    DIR *dir;
    struct dirent *ent;
    if ((dir = opendir("/proc")) == NULL) {
      perror("Scan proc failed");
      printf("\n proc failed\n");
      continue;
    }
    list = 1 - list;
    int row = 1; // [0]=size of vector; [1]=first entry, 0-terminated
    while ((ent = readdir(dir)) != NULL) {
      if (ppid_of_zombie(ent->d_name) == ppid) {
        printf(" myz> %s\n", buf);
        plist[list][row++] = atol(ent->d_name);
        if (plist[list][0] == row) {
          plist[list][0] *= 2;
          plist[list] = realloc(plist[list], sizeof(pid_t) * plist[list][0]);
        }
      }
    }
    closedir(dir);
    plist[list][row] = 0;
    if (row > 2) { // Sort lists longer than 1 entry. /proc is probably sorted, but...
      printf("q-%d ", row); fflush(stdout);
      qsort(plist[list]+1, row-1, sizeof(pid_t), pid_cmp);
      printf("q-%d\n", row); fflush(stdout);
    }

    // Wait for persistent zombies. Zigzag stepping on both lists.
    char found = 0;
    for (
        int i = 1, j = 1;
        plist[0][i] && plist[1][j]; // Quit once either list exhausted
        plist[0][i] < plist[1][j] ? ++i : ++j ) {  // Harmless to step only one or the other list
        printf("c-%d_%d ", plist[0][i], plist[1][j]); fflush(stdout);
      if ( plist[0][i] == plist[1][j] ) {
          waitpid(plist[0][i], NULL, WNOHANG); // Wait on a common zombie.
          printf("w-%d ", plist[0][i]); fflush(stdout);
      }
      found = 1;
    }
    if (found) { printf("\n"); fflush(stdout); }
  } // Forever.
  return NULL;
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
  printf("I just got loaded\n");

  pthread_t tid;
  int err = pthread_create(&tid, NULL, &doSomeThing, NULL);
  if (err != 0)
    printf("\ncan't create thread :[%s]\n", strerror(err));
  else
    printf("\n Thread created successfully\n");

}
