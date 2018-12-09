
#include <assert.h>
#include "kernel_cc.h"
#include "kernel_proc.h"
#include "kernel_streams.h"



/*
 The process table and related system calls:
 - Exec
 - Exit
 - WaitPid
 - GetPid
 - GetPPid
*/

/* The process table */
PCB PT[MAX_PROC];
unsigned int process_count;

PCB* get_pcb(Pid_t pid)
{
  return PT[pid].pstate==FREE ? NULL : &PT[pid];
}

Pid_t get_pid(PCB* pcb)
{
  return pcb==NULL ? NOPROC : pcb-PT;
}

/***Initialize a PTCB*/
PTCB* initialize_PTCB()
{
  /*Allocating space for the new PTCB*/
  PTCB* p = malloc(sizeof(PTCB));
  p->argl = 0;
  p->args = NULL;

  p->refCounter = 1;
  p->isDetached = 0;
  p->isExited = 0;
  p->wait_var = COND_INIT;


  return p;
}


/* Initialize a PCB */
static inline void initialize_PCB(PCB* pcb)
{

  pcb->pstate = FREE;
  pcb->counter = 0;

  for(int i=0;i<MAX_FILEID;i++)
    pcb->FIDT[i] = NULL;

  rlnode_init(& pcb->children_list, NULL);
  rlnode_init(& pcb->exited_list, NULL);
  rlnode_init(& pcb->ptcb_list, NULL);


  rlnode_init(& pcb->children_node, pcb);
  rlnode_init(& pcb->exited_node, pcb);

  pcb->child_exit = COND_INIT;
}


static PCB* pcb_freelist;

void initialize_processes()
{
  /* initialize the PCBs */
  for(Pid_t p=0; p<MAX_PROC; p++) {
    initialize_PCB(&PT[p]);
  }

  /* use the parent field to build a free list */
  PCB* pcbiter;
  pcb_freelist = NULL;
  for(pcbiter = PT+MAX_PROC; pcbiter!=PT; ) {
    --pcbiter;
    pcbiter->parent = pcb_freelist;
    pcb_freelist = pcbiter;
  }

  process_count = 0;

  /* Execute a null "idle" process */
  if(Exec(NULL,0,NULL)!=0)
    FATAL("The scheduler process does not have pid==0");
}


/*
  Must be called with kernel_mutex held
*/
PCB* acquire_PCB()
{
  PCB* pcb = NULL;

  if(pcb_freelist != NULL) {
    pcb = pcb_freelist;
    pcb->pstate = ALIVE;
    pcb_freelist = pcb_freelist->parent;
    process_count++;
  }

  return pcb;
}

/*
  Must be called with kernel_mutex held
*/
void release_PCB(PCB* pcb)
{
  pcb->pstate = FREE;
  pcb->parent = pcb_freelist;
  pcb_freelist = pcb;
  process_count--;
}


/*
 *
 * Process creation
 *
 */

/*
	This function is provided as an argument to spawn,
	to execute the main thread of a process.
*/
void start_main_thread()
{
  int exitval;
  TCB* current = CURTHREAD;
  Task call =  current->owner_ptcb->main_task;
  int argl = current->owner_ptcb->argl;
  void* args = current->owner_ptcb->args;

  exitval = call(argl,args);

  if(CURTHREAD==CURPROC->ptcb_list.ptcb->main_thread)
  {
    Exit(exitval);
  }
  else
  {
    ThreadExit(exitval);

  }

}


/*
	System call to create a new process.
 */
Pid_t sys_Exec(Task call, int argl, void* args)
{

  PCB *curproc, *newproc;

  /* The new process PCB */
  newproc = acquire_PCB();

  if(newproc == NULL) goto finish;  /* We have run out of PIDs! */

  if(get_pid(newproc)<=1) {
    /* Processes with pid<=1 (the scheduler and the init process)
       are parentless and are treated specially. */
    newproc->parent = NULL;
  }
  else
  {
    /* Inherit parent */
    curproc = CURPROC;

    /* Add new process to the parent's child list */
    newproc->parent = curproc;
    rlist_push_front(& curproc->children_list, & newproc->children_node);

    /* Inherit file streams from parent */
    for(int i=0; i<MAX_FILEID; i++) {
       newproc->FIDT[i] = curproc->FIDT[i];
       if(newproc->FIDT[i])
          FCB_incref(newproc->FIDT[i]);
    }
  }

/*Initializing the new PTCB*/
  PTCB* p = initialize_PTCB();
  //fprintf(stderr, "--\nPROCESS PCB : %x\n",newproc );
  //fprintf(stderr, "PROCESS TASK : %x\n",call );
  p->isDetached = 1;

  /* Set the main thread's function */
  p->main_task = call;
  /* Copy the arguments to new storage, owned by the new process */
  //newproc->argl = argl;
  p->argl = argl;

  if(args!=NULL) {
    p->args = malloc(argl);
    memcpy(p->args, args, argl);
  }
  else
    p->args=NULL;

  newproc->ptcb_list.ptcb = p;
  p->ptcb_self_node = newproc->ptcb_list;
  newproc->counter ++; //Current Threads-PCTBs
  /*
    Create and wake up the thread for the main function. This must be the last thing
    we do, because once we wakeup the new thread it may run! so we need to have finished
    the initialization of the PCB.
   */

  if(call != NULL) {
    p->main_thread = spawn_thread(newproc, start_main_thread);
    wakeup(p->main_thread);

  }


finish:
  return get_pid(newproc);
}


/* System call */
Pid_t sys_GetPid()
{
  return get_pid(CURPROC);
}


Pid_t sys_GetPPid()
{
  return get_pid(CURPROC->parent);
}


static void cleanup_zombie(PCB* pcb, int* status)
{
  if(status != NULL)
    *status = pcb->exitval;

  rlist_remove(& pcb->children_node);
  rlist_remove(& pcb->exited_node);

  release_PCB(pcb);
}


static Pid_t wait_for_specific_child(Pid_t cpid, int* status)
{

  /* Legality checks */
  if((cpid<0) || (cpid>=MAX_PROC)) {
    cpid = NOPROC;
    goto finish;
  }

  PCB* parent = CURPROC;
  PCB* child = get_pcb(cpid);
  if( child == NULL || child->parent != parent)
  {
    cpid = NOPROC;
    goto finish;
  }

  /* Ok, child is a legal child of mine. Wait for it to exit. */
  while(child->pstate == ALIVE)
    kernel_wait(& parent->child_exit, SCHED_USER);

  cleanup_zombie(child, status);

finish:
  return cpid;
}


static Pid_t wait_for_any_child(int* status)
{
  Pid_t cpid;

  PCB* parent = CURPROC;

  /* Make sure I have children! */
  if(is_rlist_empty(& parent->children_list)) {
    cpid = NOPROC;
    goto finish;
  }

  while(is_rlist_empty(& parent->exited_list)) {
    kernel_wait(& parent->child_exit, SCHED_USER);
  }

  PCB* child = parent->exited_list.next->pcb;
  assert(child->pstate == ZOMBIE);
  cpid = get_pid(child);
  cleanup_zombie(child, status);

finish:
  return cpid;
}


Pid_t sys_WaitChild(Pid_t cpid, int* status)
{
  /* Wait for specific child. */
  if(cpid != NOPROC) {
    return wait_for_specific_child(cpid, status);
  }
  /* Wait for any child */
  else {
    return wait_for_any_child(status);
  }

}


void sys_Exit(int exitval)
{
  /* Right here, we must check that we are not the boot task. If we are,
     we must wait until all processes exit. */
  if(sys_GetPid()==1) {
    while(sys_WaitChild(NOPROC,NULL)!=NOPROC);
  }
  PCB *curproc = CURPROC;  /* cache for efficiency */

  /* Do all the other cleanup we want here, close files etc. */


  /* Clean up FIDT */
  for(int i=0;i<MAX_FILEID;i++) {
    if(curproc->FIDT[i] != NULL) {
      FCB_decref(curproc->FIDT[i]);
      curproc->FIDT[i] = NULL;
    }
  }
  //fprintf(stderr, "AFTER FID WIPE ,EXIT\n" );
  /* Reparent any children of the exiting process to the
     initial task */
  PCB* initpcb = get_pcb(1);
  while(!is_rlist_empty(& curproc->children_list)) {
    rlnode* child = rlist_pop_front(& curproc->children_list);
    child->pcb->parent = initpcb;
    rlist_push_front(& initpcb->children_list, child);
  }

  /* Add exited children to the initial task's exited list
     and signal the initial task */
  if(!is_rlist_empty(& curproc->exited_list)) {
    rlist_append(& initpcb->exited_list, &curproc->exited_list);
    kernel_broadcast(& initpcb->child_exit);
  }

  /* Put me into my parent's exited list */
  if(curproc->parent != NULL) {   /* Maybe this is init */
    rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
    kernel_broadcast(& curproc->parent->child_exit);
  }

  /* Now, mark the process as exited. */
  curproc->pstate = ZOMBIE;
  curproc->exitval = exitval;


  /***Exit the remaing threads*/
  /* Bye-bye cruel world */
  sys_ThreadExit(exitval);

}
int return_NullOI()
{
  return -1;
}
/*static file_ops OpenInfo_fops =
{
  .Open = (void*)return_NullOI,
  .Read = infoRead,
  .Write = return_NullOI,
  .Close = openInfo_close
};*/

/* ------------------------------------------------------------------------------------------------
                                DEN DOULEYEI KALA NOMIZW,
                            TO AFHNW SE SXOLIA PROS TO PARON
                                      [sysinfo]
--------------------------------------------------------------------------------------------------*/

/*static file_ops OpenInfo_fops =
{
  .Open = (void*)return_NullOI,
  .Read = infoRead,
  .Write = return_NullOI,
  .Close = openInfo_close
};*/

Fid_t sys_OpenInfo()
{
  /*
  FCB** fcb = xmalloc(sizeof(FCB*));
  Fid_t* fid = xmalloc(sizeof(Fid_t));


  if(FCB_reserve(1,fid,fcb) == 0){
    fprintf(stderr, "Maximum Fid, reached\n" );
    return NOFILE;
  }

  iCB* s = xmalloc(sizeof(iCB));
  fcb[0]->streamobj = s;
  fcb[0]->streamfunc = &OpenInfo_fops;
  s->counter = 0;
  s->proccCounter = 0;
  fprintf(stderr, "CURRENT Processes %d\n",process_count );
	return fid[0];
}
int openInfo_close(void* this)
{
  iCB* s = this;
  free(s);
  return 0; */
  return NOFILE;
}


int infoRead(void* this, char *buf, unsigned int size){

  iCB* s = this;
  procinfo* p = xmalloc(sizeof(procinfo));

  while(1){
    if( PT[s->counter].pstate != FREE){
      s->proccCounter++;

      break;
    }else{
      if(s->proccCounter==process_count){
        return -1;
      }

    }
    s->counter++;
  }


  PCB* pcb = &PT[s->counter];


  p->pid = get_pid(pcb);



  p->ppid = get_pid(pcb->parent);

  if(pcb->pstate == ZOMBIE){
    p->alive = 0;
  }else{
    p->alive = 1;
  }

  p->thread_count = pcb->counter;


  PTCB* ptcb =  pcb->ptcb_list.ptcb;

  p->main_task = ptcb->main_task;

  p->argl = ptcb->argl;



  memcpy(p->args,ptcb->args,ptcb->argl);



  s->counter++;
  memcpy(buf,p,sizeof(procinfo));
  return 1;
}
