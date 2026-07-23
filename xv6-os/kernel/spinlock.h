// Mutual exclusion lock.
#ifndef _SYS_SPINLOCK_H_
#define _SYS_SPINLOCK_H_
struct spinlock {
  uint locked;       // Is the lock held?

  // For debugging:
  char *name;        // Name of lock.
  struct cpu *cpu;   // The cpu holding the lock.
};

#endif // _SYS_SPINLOCK_H_
