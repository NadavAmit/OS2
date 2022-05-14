#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

//Assignment 2 Globals --------------------------------------------

//CAS
extern uint64 cas(volatile void *addr, int expected, int newval);

//Linked Lists
struct Linked_List unused_proc_list;
struct Linked_List sleeping_proc_list;
struct Linked_List zombie_proc_list;

//End of Assignment 2 Globals---------------------------------------------------------------
extern char trampoline[]; // trampoline.S

// helps ensure that wakeups of wait()ing
// parents are not lost. helps obey the
// memory model when using p->parent.
// must be acquired before any p->lock.
struct spinlock wait_lock;

int
isListEmpty(struct Linked_List *list){
  if((list->head) == -1){
    return 1;
  }
  else return 0;
}

int 
get_list_head(struct Linked_List *list){
  acquire(&list->head_lock); 
  int head = list->head;
  release(&list->head_lock);
  return head;
}

void printLinkedList(struct Linked_List *list){
  int current = list->head;
  struct proc *p;
  if(current != -1){
    p=&proc[list->head];
  }
  printf("%s \n",list->list_name);
  printf("\n[");
  while(current !=-1){
    printf("%d->", current);
    p=&proc[current];
    current = p->nextProcessInList;
  }
  printf(" ]\n");
}

//Initialize lists
void
linkedListsInit(void){

  //initialize global linked lists locks and head node
  initlock(&unused_proc_list.head_lock, "unused_list_head_lock");
  initlock(&sleeping_proc_list.head_lock, "sleeping_list_head_lock");
  initlock(&zombie_proc_list.head_lock, "zombie_list_head_lock");

  unused_proc_list.head= -1;
  sleeping_proc_list.head= -1;
  zombie_proc_list.head= -1;

  //initialize names
  unused_proc_list.list_name = "unused_proc_list";
  sleeping_proc_list.list_name = "sleeping_proc_list";
  zombie_proc_list.list_name = "zombie_proc_list";

  //initialize CPU's linked lists locks and head node
  struct cpu *c;
  for(c = cpus; c < &cpus[NCPU]; c++) {
      initlock(&c->runnable_proc_list.head_lock, "runnable_list_head_lock");
      c->runnable_proc_list.head = -1;
      c->runnable_proc_list.list_name = "cpu_runnable_proc_list";
  }
}
// Allocate a page for each process's kernel stack.
// Map it high in memory, followed by an invalid
// guard page.
void
proc_mapstacks(pagetable_t kpgtbl) {
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// initialize the proc table at boot time.
void
procinit(void)
{
  struct proc *p;
  linkedListsInit();

  int proccessIndex = 0;
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");

      //procces initialziation
      p->procIndex=proccessIndex;
      proccessIndex++;
      p->nextProcessInList = -1;
      p->assignedCPU =-1;

      //add the to the unused list
      insertProcessToList(p,&unused_proc_list);

      p->kstack = KSTACK((int) (p - proc));
  }
}
void
insertProcessToList(struct proc *p,struct Linked_List *list){
  struct proc *currentProc;
  struct proc *prevProc;
  int listHeadIndex;

  //acquire the head lock
  // printf("before insertProcessToList acquire\n");//delete
  acquire(&list->head_lock);
  listHeadIndex = list->head;
  // printf("after insertProcessToList acquire\n");//delete
 
  //In case the list is empty
  if(listHeadIndex == -1){
    list->head = p->procIndex;
    p->nextProcessInList = -1;
    release(&list->head_lock);
  }
  
  //list isn't empty
  else{
    prevProc = &proc[listHeadIndex];

    // printf("before insertProcessToList.else acquire\n");  //delete

    acquire(&prevProc->nodeLock);
    release(&list->head_lock);
    // printf("after insertProcessToList.else acquire\n"); //delete

    //if list contains exactly one node
    if(prevProc->nextProcessInList == -1){
    // printf("inserting process pid: %d to list: %s \n",p->procIndex,list->list_name);
      prevProc->nextProcessInList= p->procIndex;
      p->nextProcessInList = -1;
      release(&prevProc->nodeLock);
    }


   //list contains two nodes or more
    else{
      currentProc = &proc[prevProc->nextProcessInList];
      acquire(&currentProc->nodeLock);

      //itirate until you get to the final node of the list
      while((currentProc->nextProcessInList)!= -1){
        // printf("before insertProcessToList.while acquire\n"); //delete
        release(&prevProc->nodeLock);
        prevProc = currentProc;
        currentProc = &proc[currentProc->nextProcessInList];
        acquire(&currentProc->nodeLock);
        // printf("after insertProcessToList.while acquire\n"); //delete        
      }
    
      //found last node
      //update next node and insert to list
      release(&prevProc->nodeLock);
      //printf("inserting process pid: %d to list: %s \n",p->procIndex,list->list_name);
      currentProc->nextProcessInList = p->procIndex;
      p->nextProcessInList = -1;
      release(&currentProc->nodeLock);
      }

  }
}

void
removeProcessFromList(struct proc *p,struct Linked_List *list){
  struct proc *currentProc;
  struct proc *prevProc;
  // printf("zombie list head head points to: %d\n",zombie_proc_list.nextProcessInList);
  // printf("list Dummy head points to: %d\n",&listDummyHead->nextProcessInList);
  //printLinkedList(list);
  //acquire the head lock
  // printf("before removeproccesFromList acquire\n"); //delete

  acquire(&list->head_lock);
  // printf("after removeproccesFromList acquire\n"); //delete

  //In case the list is empty - ERROR We cant delete
  if(list->head == -1){
    release(&list->head_lock);
    panic("Error removing process - empty list");
  }

  //list is not empty
  //acquire the real head lock
  // printf("before removeproccesFromList.realheadlock acquire\n"); //delete
  prevProc=&proc[list->head];
  acquire(&prevProc->nodeLock);                               //pointers to two nodes
  release(&list->head_lock);

  if(prevProc->procIndex == p->procIndex){
    if(prevProc->nextProcessInList == -1){
    list->head = -1;
    }
    else{
      list->head = prevProc->nextProcessInList;
    }
    release(&prevProc->nodeLock);
    return;

  }

  //only one node and it's not what we searched for
  if(prevProc->nextProcessInList == -1){
    release(&prevProc->nodeLock);
    panic("Error removing process - Node not found");
    return;
  }

  currentProc = &proc[prevProc->nextProcessInList];
  acquire(&currentProc->nodeLock);
  // printf("after removeproccesFromList.realheadlock acquire\n"); //delete
   
  //  prevProc=listDummyHead;                               //pointers to two nodes
  //  currentProc = &proc[listHeadIndex];
    //printf("before while with index: %d \n",currentProc->procIndex);
    //printf("needed index: %d ,/t actual index: %d\n ",p->procIndex,currentProc->procIndex);

  //while the needed procces doesn't found or you didn't arrive to the end of the list
  while (((currentProc->procIndex) != (p->procIndex)) && (currentProc->nextProcessInList != -1)){
    // printf("inside while with index: %d \n",currentProc->procIndex);

    // printf("needed index: %d ,/t actual index: %d\n ",p->procIndex,currentProc->procIndex);
    release(&prevProc->nodeLock);
    prevProc = currentProc;
    currentProc = &proc[prevProc->nextProcessInList];
    acquire(&currentProc->nodeLock);
  // printf("before removeproccesFromList.while acquire\n"); //delete

    
  // printf("after removeproccesFromList.while acquire\n"); //delete

  }
  
  //found the right node to delete
  //delete node and detach it 
  if(currentProc->procIndex == p->procIndex){
    prevProc->nextProcessInList = currentProc->nextProcessInList;
    currentProc->nextProcessInList = -1;
    release(&prevProc->nodeLock);
    release(&currentProc->nodeLock);
    return;
  }
  //if we reached the end of the list then not found -FAIL
  else{
    release(&prevProc->nodeLock);
    release(&currentProc->nodeLock);
    panic("Error removing procces - process not found in list");
  }
  

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
mycpu(void) {
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// Return the current struct proc *, or zero if none.
struct proc*
myproc(void) {
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

//Original allocpid
// int
// allocpid() {
//   int pid;
  
//   acquire(&pid_lock);
//   pid = nextpid;
//   nextpid = nextpid + 1;
//   release(&pid_lock);

//   return pid;
// }

int
allocpid() {  
  int old;
  do{
    old = nextpid;
  }while(cas(&nextpid ,old ,old+1));

  return old;
}


// Look in the process table for an UNUSED proc.
// If found, initialize state required to run in the kernel,
// and return with p->lock held.
// If there are no free procs, or a memory allocation fails, return 0.
static struct proc*
allocproc(void)
{
  //printLinkedList(&unused_proc_list);
  struct proc *p;

  if(!isListEmpty(&unused_proc_list))
 {
   p=&proc[get_list_head(&unused_proc_list)];
    acquire(&p->lock);
    if(p->state == UNUSED) {
      removeProcessFromList(p,&unused_proc_list);
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->state = USED;

  // Allocate a trapframe page.
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // An empty user page table.
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // Set up new context to start executing at forkret,
  // which returns to user space.
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// free a proc structure and the data hanging from it,
// including user pages.
// p->lock must be held.
static void
freeproc(struct proc *p)
{
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->state = UNUSED;

  p->nextProcessInList =-1;
  p->assignedCPU =-1;
  removeProcessFromList(p,&zombie_proc_list);

  insertProcessToList(p,&unused_proc_list);

}

// Create a user page table for a given process,
// with no user memory, but with trampoline pages.
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

  // map the trapframe just below TRAMPOLINE, for trampoline.S.
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
// od -t xC initcode
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
  
  // allocate one user page and copy init's instructions
  // and data into it.
  uvminit(p->pagetable, initcode, sizeof(initcode));
  p->sz = PGSIZE;

  // prepare for the very first "return" from kernel to user.
  p->trapframe->epc = 0;      // user program counter
  p->trapframe->sp = PGSIZE;  // user stack pointer

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  p->state = RUNNABLE;
  
  //insert first procc to the first cpu
  struct cpu *firstCPU = &cpus[0];
  p->assignedCPU = 0;
  insertProcessToList(p,&(firstCPU->runnable_proc_list));

  release(&p->lock);
}

// Grow or shrink user memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if((sz = uvmalloc(p->pagetable, sz, sz + n)) == 0) {
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
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();
  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }
  // Copy user memory from parent to child.
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
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
  acquire(&np->nodeLock);
  np->assignedCPU = p->assignedCPU;
  release(&np->nodeLock);
  struct cpu *c = &cpus[np->assignedCPU];
  insertProcessToList(np,&(c->runnable_proc_list));
  
  return pid;
}

// Pass p's abandoned children to init.
// Caller must hold wait_lock.
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
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

  printf("waking up parrent\n");
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;


  acquire(&p->nodeLock);
  p->assignedCPU = -1;
  release(&p->nodeLock);
  insertProcessToList(p,&zombie_proc_list);


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
  struct proc *np;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(np = proc; np < &proc[NPROC]; np++){
      if(np->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&np->lock);

        havekids = 1;
        if(np->state == ZOMBIE){
          // Found one.
          pid = np->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&np->xstate,
                                  sizeof(np->xstate)) < 0) {
            release(&np->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(np);
          release(&np->lock);
          release(&wait_lock);
          return pid;
        }
        release(&np->lock);
      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || p->killed){
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
  struct proc *p;
  struct cpu *c = mycpu();
  
  c->proc = 0;
  for(;;){
    // Avoid deadlock by ensuring that devices can interrupt.
    intr_on();

    while(!(isListEmpty(&(c->runnable_proc_list)))){
      int cpu_list_head_index = get_list_head(&(c->runnable_proc_list));
      if(cpu_list_head_index != -1) {
        p = (&proc[cpu_list_head_index]);
        acquire(&p->lock);

        if(p->state == RUNNABLE) {

          // Switch to chosen process.  It is the process's job
          // to release its lock and then reacquire it
          // before jumping back to us.

          removeProcessFromList(p,&(c->runnable_proc_list));
          p->state = RUNNING;
          c->proc = p;
          
          //printLinkedList(&unused_proc_list);
          swtch(&c->context, &p->context);

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
        release(&p->lock);
      }
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

// Give up the CPU for one scheduling round.
void
yield(void)
{
  struct proc *p = myproc();
  struct cpu *c = mycpu();

  acquire(&p->lock);
  p->state = RUNNABLE;

  //Process is giving up its running time so re-inserting it to the end of runnable list
  insertProcessToList(p,&(c->runnable_proc_list));
  
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
    first = 0;
    fsinit(ROOTDEV);
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
  //inserting procces to Sleeping linked list
  //printf("inside sleep -before insert process to sleep\n"); //delete
  insertProcessToList(p,&sleeping_proc_list);
  //printf("inside sleep -after insert process to sleep\n"); //delete
 
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
  struct proc *p;
  struct cpu *c;
  int currentpid = get_list_head(&sleeping_proc_list);
// printf("before wakeup while while \n");
//printLinkedList(&sleeping_proc_list);

  while((currentpid != -1))
  {
    // printf("inside wakeup while while \n");
    p=&proc[currentpid];
    currentpid = p->nextProcessInList;
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {

        //removing procces from sleeping list
        // printf("inside wakeup - before remove fromlist\n");//delete
        //printLinkedList(&sleeping_proc_list);
        removeProcessFromList(p,&sleeping_proc_list);
        // printf("inside wakeup - after remove fromlist\n");//delete

        p->state = RUNNABLE;
        
        //inserting procces to it's CPU runnable list
        c=&cpus[p->assignedCPU];
        insertProcessToList(p,&(c->runnable_proc_list));

      }
      release(&p->lock);
    }
    // currentpid=p->nextProcessInList;
    // printf("end of wakeup while currentpid value: %d \n",currentpid);

  }
  // printf("after wakeup while while \n");
}

// Kill the process with the given pid.
// The victim won't exit until it tries to return
// to user space (see usertrap() in trap.c).
int
kill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // Wake process from sleep().
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
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
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
int
set_cpu(int cpu_num){
struct proc *p;

  //number not in range
  if(cpu_num<0 || cpu_num >= NCPU){
    return -1;
  }

  else{

  p = myproc();
  // c=&cpus[cpu_num];


  acquire(&p->nodeLock);
  
  //The procces is running so its in RUNNABLE state
  //insertProcessToList(p,&(c->runnable_proc_list));
  p->assignedCPU = cpu_num;
 
  release(&p->nodeLock);
 
  yield();
  return cpu_num;
  }
}
int get_cpu(){
  return cpuid();
}
