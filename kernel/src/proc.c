#include "klib.h"
#include "cte.h"
#include "proc.h"

#define PROC_NUM 64

static __attribute__((used)) int next_pid = 1;

proc_t pcb[PROC_NUM];
static proc_t *curr = &pcb[0];

void init_proc() {
  // Lab2-1, set status and pgdir
  // Lab2-4, init zombie_sem
  // Lab3-2, set cwd
  curr->status = RUNNING;
  curr->pgdir = vm_curr();
  curr->kstack = (void*)(KER_MEM-PGSIZE);
  sem_init(&(curr->zombie_sem), 0);
  for(int i = 0;i < MAX_USEM;i++){
    curr->usems[i] = NULL;
  }
}

proc_t *proc_alloc() {
  // Lab2-1: find a unused pcb from pcb[1..PROC_NUM-1], return NULL if no such one
  // TODO();
  // init ALL attributes of the pcb
  for(int i = 1; i < PROC_NUM;i++){
    if(pcb[i].status == UNUSED){
      pcb[i].pid = next_pid;
      next_pid++;
      if(next_pid == 32767){next_pid = 1;}
      pcb[i].status = UNINIT;
      PD* pgdir = vm_alloc();
      pcb[i].pgdir = pgdir;
      pcb[i].brk = 0;
      pcb[i].kstack = kalloc();
      pcb[i].ctx = &(pcb[i].kstack->ctx);
      pcb[i].child_num = 0;
      pcb[i].parent = NULL;
      sem_init(&(pcb[i].zombie_sem), 0);
      for(int j = 0;j < MAX_USEM;j++){
        pcb[i].usems[j] = NULL;
      }
      return &pcb[i];
    }
  }
  return NULL;
}

void proc_free(proc_t *proc) {
  // Lab2-1: free proc's pgdir and kstack and mark it UNUSED
  // TODO();
  vm_teardown(proc->pgdir);
  kfree(proc->kstack);
  proc->status = UNUSED;
}

proc_t *proc_curr() {
  return curr;
}

void proc_run(proc_t *proc) {
  proc->status = RUNNING;
  curr = proc;
  set_cr3(proc->pgdir);
  set_tss(KSEL(SEG_KDATA), (uint32_t)STACK_TOP(proc->kstack));
  irq_iret(proc->ctx);
}

void proc_addready(proc_t *proc) {
  // Lab2-1: mark proc READY
  proc->status = READY;
}

void proc_yield() {
  // Lab2-1: mark curr proc READY, then int $0x81
  curr->status = READY;
  INT(0x81);
  // printf("proc_yield");
}

void proc_copycurr(proc_t *proc) {
  // Lab2-2: copy curr proc
  // Lab2-5: dup opened usems
  // Lab3-1: dup opened files
  // Lab3-2: dup cwd
  // TODO();
  proc_t *nowpr = proc_curr();
  vm_copycurr(proc->pgdir);
  proc->brk = nowpr->brk;
  proc->kstack->ctx = nowpr->kstack->ctx;
  * proc->ctx = nowpr->kstack->ctx;
  proc->ctx->eax = 0;
  proc->parent = nowpr;
  nowpr->child_num ++;
  for(int i = 0;i < MAX_USEM;i++){
    proc->usems[i] = nowpr->usems[i];
    if (proc->usems[i] != NULL) {
      // TODO:不太确定
      usem_dup(proc->usems[i]);  // 使用 usem_dup 复制信号量
    }
  }
}

void proc_makezombie(proc_t *proc, int exitcode) {
  // Lab2-3: mark proc ZOMBIE and record exitcode, set children's parent to NULL
  // Lab2-5: close opened usem
  // Lab3-1: close opened files
  // Lab3-2: close cwd
  // TODO();
  proc->status = ZOMBIE;
  proc->exit_code = exitcode;
  for(int i = 0;i < PROC_NUM ;i++){
    if (pcb[i].parent==proc){
      pcb[i].parent=NULL;
    }
  }
  if(proc->parent != NULL){
    sem_v(&(proc->parent->zombie_sem));
  }
  for(int i = 0;i < MAX_USEM;i++){
    // printf("似在这里的124");
    if(proc->usems[i]!=NULL)
      usem_close(proc->usems[i]);
  }
}

proc_t *proc_findzombie(proc_t *proc) {
  // Lab2-3: find a ZOMBIE whose parent is proc, return NULL if none
  // TODO();
  for(int i = 0;i < PROC_NUM;i++){
    if(pcb[i].parent == proc && pcb[i].status == ZOMBIE){
      return &pcb[i];
    }
  }
  return NULL;
}

void proc_block() {
  // Lab2-4: mark curr proc BLOCKED, then int $0x81
  curr->status = BLOCKED;
  INT(0x81);
}

int proc_allocusem(proc_t *proc) {
  // Lab2-5: find a free slot in proc->usems, return its index, or -1 if none
  // TODO();
  for(int i = 0;i < MAX_USEM;i++){
    if(proc->usems[i] == NULL){
      return i;
    }
  }
  return -1;
}

usem_t *proc_getusem(proc_t *proc, int sem_id) {
  // Lab2-5: return proc->usems[sem_id], or NULL if sem_id out of bound
  // TODO();
  if(sem_id >= MAX_USEM || sem_id < 0){
    return NULL;
  }
  return proc->usems[sem_id];
}

int proc_allocfile(proc_t *proc) {
  // Lab3-1: find a free slot in proc->files, return its index, or -1 if none
  TODO();
}

file_t *proc_getfile(proc_t *proc, int fd) {
  // Lab3-1: return proc->files[fd], or NULL if fd out of bound
  TODO();
}

void schedule(Context *ctx) {
  // Lab2-1: save ctx to curr->ctx, then find a READY proc and run it
  // TODO();
  curr->ctx = ctx;
  int cur_pos = 0;
  // 不能直接从头开始
  for(int i = 0;i < PROC_NUM;i++){
    if(proc_curr() == &pcb[i]){
      cur_pos = i;
      break;
    }
  }
  for(int i = 1;i <= PROC_NUM;i++){
    int position = (cur_pos + i) % PROC_NUM;
    if(pcb[position].status == READY){
      proc_run(&pcb[position]);
      return;
    }
  }
  printf("没有找到对应的Ready的");
  return;
}
