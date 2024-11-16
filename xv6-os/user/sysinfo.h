#ifndef _SYSINFO_H_
#define _SYSINFO_H_

struct sysinfo {
  int procs_cnt;
  int open_files;
  int used_ram;
  int free_ram;
};

#endif // _SYSINFO_H_
