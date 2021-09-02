#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;

  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");

  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;
  p->queue_num = HRRN;

  p->cycles = 1;
  p->ticket = 10;
  p->waiting_time = 0;
  acquire(&tickslock);
  p->arrival_time = ticks;
  release(&tickslock);

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();

  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(void)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // Found one.
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.

struct proc* 
get_lottery_sched_proc(void)
{
  struct proc *p;
  uint total_tickets = 0;
  uint cur_tickets = 0;
  uint goal_ticket;
  uint random;
  Bool has_proc = FALSE;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE || p->queue_num != LOTTERY)
      continue;

    total_tickets += p->ticket;
    has_proc = TRUE;
  }

  //acquire(&tickslock);
  //random = ticks;
  //release(&tickslock);

  if(has_proc){
    random = ticks;
    goal_ticket = (random) % total_tickets;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE || p->queue_num != LOTTERY)
        continue;

      cur_tickets += p->ticket;
    
      if(goal_ticket < cur_tickets){
        return p;
      }
    }
  }
  return NOTHING;
}

struct proc*
get_round_robin_sched_proc(void)
{
  struct proc *p;
  struct proc *target_proc;
  Bool has_proc = FALSE;
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE || p->queue_num != ROUND_ROBIN)
        continue;
    if(has_proc)
    {
        if(p->arrival_time < target_proc->arrival_time)
          target_proc = p;
    }
    else
    {
        target_proc = p;
        has_proc = TRUE;
    }
  }

  if(has_proc)
    return target_proc;

  return NOTHING;
}

double calculate_hrrn(int arrival_time, int cycles)
{
    //acquire(&tickslock);
    int current_time = ticks;
    //release(&tickslock);
    int waiting_time = current_time - arrival_time;
    // cprintf("waiting time = %d, cycles = %d\n", waiting_time, cycles);
    double hrrn = waiting_time * 1.0 / cycles ;
    return hrrn;
}

struct proc*
get_hrrn_sched_proc(void)
{
  struct proc *current_proc;
  struct proc *max_ratio_proc = 0;
  double max_ratio = 0.0;

  for(current_proc = ptable.proc; current_proc < &ptable.proc[NPROC]; ++current_proc){
    if(current_proc->state != RUNNABLE || current_proc->queue_num != HRRN)
      continue;

    double current_ratio = calculate_hrrn(current_proc->arrival_time, current_proc->cycles);

    if (current_ratio > max_ratio)
    {
      max_ratio = current_ratio;
      max_ratio_proc = current_proc;
    }
  }

  return max_ratio_proc;
}

void
update_waiting_times()
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
      continue;

    p->waiting_time++;
  }
}

void
check_aging()
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state != RUNNABLE)
      continue;

    if (p->waiting_time > AGING_CYCLE)
    {
        p->queue_num = LOTTERY;
        p->waiting_time = 0;
    }
  }
}

void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;

  for(;;){
    // Enable interrupts on this processor.
    sti();

    acquire(&ptable.lock);

    p = get_lottery_sched_proc();

    if(p == NOTHING)
      p = get_round_robin_sched_proc();

    if(p == NOTHING)
      p = get_hrrn_sched_proc();

    // Switch to chosen process.  It is the process's job
    // to release ptable.lock and then reacquire it
    // before jumping back to us.
    if(p != NOTHING)
    {
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
        p->cycles += 0.1;
        update_waiting_times();
        p->waiting_time = 0;
        check_aging();
        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        c->proc = 0;
        if (p->state == RUNNABLE && p->queue_num == ROUND_ROBIN)
        {
            //acquire(&tickslock);
            p->arrival_time = ticks;
            //release(&tickslock);
        }
    }

    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();

  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

int
set_proc_queue(int pid, int dest_queue)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->queue_num = dest_queue;
      p->waiting_time = 0;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

int
set_proc_ticket(int pid, int value)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->ticket = value;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

void print_spaces(int remaining)
{
  int i;
  if (remaining <= 0)
    return;
  for (i = 0; i < remaining; i++)
    cprintf(" ");
}

int
count_num_of_digits(int number)
{
  int count = 0;
  if (number == 0)
    count++;
  else
  {
    while(number != 0)
    {
      count++;
      number /= 10;
    }
  }
  return count;
}

void reverse(char* str, int len) 
{ 
    int i = 0, j = len - 1, temp; 
    while (i < j) { 
        temp = str[i]; 
        str[i] = str[j]; 
        str[j] = temp; 
        i++; 
        j--; 
    } 
} 
  
int integer_to_string(int x, char str[], int d) 
{ 
    int i = 0; 

    if(x == 0)
      str[i++] = '0';

    while (x) { 
        str[i++] = (x % 10) + '0'; 
        x = x / 10; 
    } 
  
    while (i < d) 
        str[i++] = '0'; 
  
    reverse(str, i); 
    str[i] = '\0'; 
    return i; 
} 

int pow(int x, unsigned int y) 
{ 
    if (y == 0) 
        return 1; 
    else if (y % 2 == 0) 
        return pow(x, y / 2) * pow(x, y / 2); 
    else
        return x * pow(x, y / 2) * pow(x, y / 2); 
} 
  
void float_to_string(float number, char* res, int precision) 
{ 
    int ipart = (int)number; 
  
    float fpart = number - (float)ipart; 
  
    int i = integer_to_string(ipart, res, 0); 
  
    if (precision != 0) { 
        res[i] = '.'; 
  
        fpart = fpart * pow(10, precision); 
  
        integer_to_string((int)fpart, res + i + 1, precision); 
    } 
}

int
print_processes(void)
{
  static char *states[] = {
  [UNUSED]    "UNUSED",
  [EMBRYO]    "EMBRYO",
  [SLEEPING]  "SLEEPING",
  [RUNNABLE]  "RUNNABLE",
  [RUNNING]   "RUNNING",
  [ZOMBIE]    "ZOMBIE"
  };

  enum titles {NAME, PID, STATE, QUEUE_NUM, TICKET, CYCLES, HRRN_TITLE};
  static const int table_columns = 7;

  static const char *titles_str[] = {
    [NAME]        "name",
    [PID]         "pid",
    [STATE]       "state",
    [QUEUE_NUM]   "queue_num",
    [TICKET]      "ticket",
    [CYCLES]      "cycles",
    [HRRN_TITLE]  "HRRN"
  };
  int min_space_between_words = 4;
  int max_column_lens[] = {
    [NAME]        15 + min_space_between_words,
    [PID]         strlen(titles_str[PID]) + min_space_between_words,
    [STATE]       8 + min_space_between_words,
    [QUEUE_NUM]   strlen(titles_str[QUEUE_NUM]) + min_space_between_words,
    [TICKET]      strlen(titles_str[TICKET]) + min_space_between_words,
    [CYCLES]      strlen(titles_str[CYCLES]) + min_space_between_words,
    [HRRN_TITLE]  5 + min_space_between_words
  };

  int i;
  for (i = 0; i < table_columns; i++)
  {
    cprintf("%s", titles_str[i]);
    print_spaces(max_column_lens[i] - strlen(titles_str[i]));
  }
  cprintf("\n---------------------------------------------------------------------------------\n");

  struct proc *p;
  char *state;
  int ticket_len;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++) {
    if (p->pid == 0)
      continue;
    state = states[p->state];
    cprintf("%s", p->name);
    print_spaces(max_column_lens[NAME] - strlen(p->name));
    cprintf("%d", p->pid);
    print_spaces(max_column_lens[PID] - count_num_of_digits(p->pid));
    cprintf("%s", state);
    print_spaces(max_column_lens[STATE] - strlen(state));
    cprintf("%d", p->queue_num);
    print_spaces(max_column_lens[QUEUE_NUM] - count_num_of_digits(p->queue_num));
    if (p->queue_num != LOTTERY)
    {
      cprintf("--");
      ticket_len = 2;
    }
    else
    {
      cprintf("%d", p->ticket);
      ticket_len = count_num_of_digits(p->ticket);
    }    
    print_spaces(max_column_lens[TICKET] - ticket_len);
    
    char cycles_str[30];
    float_to_string(p->cycles, cycles_str, CYCLES_PRECISION);

    cprintf("%s", cycles_str);
    print_spaces(max_column_lens[CYCLES] - strlen(cycles_str));
    
    double hrrn_ratio = calculate_hrrn(p->arrival_time, p->cycles);

    char hrrn_str[30];
    float_to_string(hrrn_ratio, hrrn_str, HRRN_PRECISION);

    cprintf("%s\n", hrrn_str);
    cprintf("\n");
  }
  release(&ptable.lock);
  return 0;
}
