#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "list.h"
#include "proc.h"
#include "defs.h"

#define MAX_PRIORITY_DEPTH 5

struct cpu cpus[NCPU];

// list, where i will push procs dynamically to
struct list proc_list;

// count procs to pass forkforkfork() test
int proc_count = 0;

// new: lock for introduced proc list
// note: must be acquired after waitlock, but before any other locks.
struct spinlock proc_list_lock;

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

// initialize the proc table.
void
procinit(void)
{
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");

  // new: lock for proc list
  lst_init(&proc_list);
  initlock(&proc_list_lock, "proc_list_lock");
}

// Must be called with interrupts disabled,
// to prevent race with process being moved
// to a different CPU.
int
cpuid()
{
  int id = r_tp();
  return id;
}

// Return this CPU's cpu struct.
// Interrupts must be disabled.
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// new: remove proc from proc_list
// note: proc_list_lock must be held.
static void proc_remove(struct proc *p) {
  if (lst_empty(&proc_list)) {
    panic("proc_remove");
  }
  lst_remove(&p->ple);
  proc_count--;
}

// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  // new: use bd_malloc for proc struct
  struct proc *p = (struct proc *) bd_malloc(sizeof(struct proc));
  if (p == 0)
    return 0;

  memset(p, 0, sizeof(struct proc));

  initlock(&p->lock, "proc");
  acquire(&p->lock);

  p->pid = allocpid();
  p->state = USED;
  p->priority = 0; // set it to 0 for clarity, even tho memset does this fine

  // Allocate a stack, a trapframe page,
  // and a page table
  if ((p->kstack = (uint64) kalloc()) == 0 ||
      (p->trapframe = (struct trapframe *)kalloc()) == 0 ||
      (p->pagetable = proc_pagetable(p)) == 0) {
    release(&p->lock);
    freeproc(p);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  // new: push proc to a proc list
  acquire(&proc_list_lock);
  lst_push(&proc_list, &p->ple);
  proc_count++;
  release(&proc_list_lock);

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  // new: free proc's stack
  if (p->kstack)
    kfree((void*)p->kstack);
  if(p->trapframe)
    kfree((void*)p->trapframe);
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);

  // new: just free a proc with buddy allocator
  bd_free(p);
}

// Create a user page table for a given process, with no user memory,
// but with trampoline and trapframe pages.
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // map the trampoline code (for system call return)
  // at the highest user virtual address.
  // only the supervisor uses it, on the way
  // to/from user space, so not PTE_U.
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // map the trapframe page just below the trampoline page, for
  // trampoline.S.
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// Free a process's page table, and free the
// physical memory it refers to.
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// a user program that calls exec("/init")
// assembled from ../user/initcode.S
// od -t xC ../user/initcode
uchar initcode[] = {
  0x17, 0x05, 0x00, 0x00, 0x13, 0x05, 0x45, 0x02,
  0x97, 0x05, 0x00, 0x00, 0x93, 0x85, 0x35, 0x02,
  0x93, 0x08, 0x70, 0x00, 0x73, 0x00, 0x00, 0x00,
  0x93, 0x08, 0x20, 0x00, 0x73, 0x00, 0x00, 0x00,
  0xef, 0xf0, 0x9f, 0xff, 0x2f, 0x69, 0x6e, 0x69,
  0x74, 0x00, 0x00, 0x24, 0x00, 0x00, 0x00, 0x00,
  0x00, 0x00, 0x00, 0x00
};

// Set up first user process.
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  // allocate one user page and copy initcode's instructions
  // and data into it.
  uvmfirst(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// Create a new process, copying the parent.
// Sets up child kernel stack to return as if from fork() system call.
int
fork(void)
{
  // new: forkforkfork test passage
  // note: just a hard limit for a number of procs (NPROC = 64 currently)
  acquire(&proc_list_lock);
  if(proc_count + 1 >= NPROC){
    release(&proc_list_lock);
    return -1;
  }
  release(&proc_list_lock);

  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    release(&np->lock);

    // new: remove proc from proc list
    acquire(&proc_list_lock);
    proc_remove(p);
    release(&proc_list_lock);
    freeproc(np);
    return -1;
  }
  np->sz = p->sz;

  // copy saved user registers.
  *(np->trapframe) = *(p->trapframe);

  // Cause fork to return 0 in the child.
  np->trapframe->a0 = 0;

  // increment reference counts on open file descriptors.
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct list *ple;
  struct proc *pp;
  int need_wakeup = 0;

  acquire(&proc_list_lock);
  // new: change parent for an abandoned proc
  for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
    pp = (struct proc *) ple;
    if(pp->parent == p){
      pp->parent = initproc;
      // note: can't wakeup while inside this lock
      // feature, not a bug
      need_wakeup = 1;
    }
  }
  release(&proc_list_lock);
  if (need_wakeup)
    wakeup(initproc);
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait().
void
exit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  acquire(&wait_lock);

  // Give any children to init.
  reparent(p);

  // Parent might be sleeping in wait().
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  release(&wait_lock);

  // Jump into the scheduler, never to return.
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(uint64 addr)
{
  struct list *ple;
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    acquire(&proc_list_lock);
    for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
      pp = (struct proc *)ple;
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // Found one.
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&proc_list_lock);
            release(&wait_lock);
            return -1;
          }
          release(&pp->lock);
          proc_remove(pp);
          release(&proc_list_lock);
          release(&wait_lock);
          freeproc(pp);
          return pid;
        }
        release(&pp->lock);
      }
    }
    release(&proc_list_lock);

    // No point waiting if we don't have any children.
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // Wait for a child to exit.
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run.
//  - swtch to start running that process.
//  - eventually that process transfers control
//    via swtch back to the scheduler.
void
scheduler(void)
{
  struct list *ple;
  struct proc *p;
  struct cpu *c = mycpu();

  c->proc = 0;
  for(;;){
    // The most recent process to run may have had interrupts
    // turned off; enable them to avoid a deadlock if all
    // processes are waiting.
    intr_on();

    int found = 0;
    int min = MAX_PRIORITY_DEPTH + 1;
    acquire(&proc_list_lock);
    struct proc *selected = 0;
    for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
      p = (struct proc *) ple;
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->priority < min) {
        if (selected) {
          release(&p->lock);
          release(&selected->lock);
          acquire(&p->lock);
        }
        selected = p;
        min = p->priority;
      } else {
        release(&p->lock);
          
      }
    }
    release(&proc_list_lock);

    if (selected) {
      // Switch to chosen process.  It is the process's job
      // to release its lock and then reacquire it
      // before jumping back to us.
      selected->state = RUNNING;
      c->proc = selected;
      swtch(&c->context, &selected->context);

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
      found = 1;
      release(&selected->lock);

      // FCFS: push proc to the end of the list to ensure
      // fisrt-come-first-served among 1 priority queue
      acquire(&proc_list_lock);
      lst_remove(&selected->ple);
      lst_push(&proc_list, &selected->ple);
      release(&proc_list_lock);
    }
    if(found == 0) {
      // nothing to run; stop running on this core until an interrupt.
      intr_on();
      asm volatile("wfi");
    }
  }
}

// Switch to scheduler.  Must hold only p->lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->noff, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// utility
int min(int a, int b) {
  return a < b ? a : b;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->priority = min(p->priority + 1, MAX_PRIORITY_DEPTH);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch to forkret.
void
forkret(void)
{
  static int first = 1;

  // Still holding p->lock from scheduler.
  release(&myproc()->lock);

  if (first) {
    // File system initialization must be run in the context of a
    // regular process (e.g., because it calls sleep), and thus cannot
    // be run from main().
    fsinit(ROOTDEV);

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();
  }

  usertrapret();
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // Must acquire p->lock in order to
  // change p->state and then call sched.
  // Once we hold p->lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup locks p->lock),
  // so it's okay to release lk.

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// Wake up all processes sleeping on chan.
// Must be called without any p->lock.
void
wakeup(void *chan)
{
  struct list *ple;
  struct proc *p;

  acquire(&proc_list_lock);
  for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
    p = (struct proc *) ple;
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
  release(&proc_list_lock);
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct list *ple;
  struct proc *p;

  acquire(&proc_list_lock);
  for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
    p = (struct proc *) ple;
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      release(&proc_list_lock);
      return 0;
    }
    release(&p->lock);
  }
  release(&proc_list_lock);
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// Copy to either a user address, or kernel address,
// depending on usr_dst.
// Returns 0 on success, -1 on error.
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// Copy from either a user address, or kernel address,
// depending on usr_src.
// Returns 0 on success, -1 on error.
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct list *ple;
  struct proc *p;
  char *state;

  printf("\n");
  acquire(&proc_list_lock);
  for(ple = proc_list.next; ple != &proc_list; ple = ple->next){
    p = (struct proc *) ple;
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
  release(&proc_list_lock);
}
