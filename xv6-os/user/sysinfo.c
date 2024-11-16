#include "user.h"
#include "sysinfo.h"

int main() {
  struct sysinfo sysinfo;

  if (info(&sysinfo) < 0) {
    printf("Error bro, better luck next time");
    exit(0);
  }

  printf("System info:\n");
  printf(" Number of procs: %d\n", sysinfo.procs_cnt);
  printf(" Number of open files: %d\n", sysinfo.open_files);

  exit(0);
}
