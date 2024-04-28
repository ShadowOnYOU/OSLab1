#include "klib.h"
#include "vme.h"
#include "proc.h"

static TSS32 tss;

typedef union free_page {
  union free_page *next;
  char buf[PGSIZE];
} page_t;

static page_t *free_page_list = NULL;

void init_gdt() {
  static SegDesc gdt[NR_SEG];
  gdt[SEG_KCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_KERN);
  gdt[SEG_KDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_KERN);
  gdt[SEG_UCODE] = SEG32(STA_X | STA_R,   0,     0xffffffff, DPL_USER);
  gdt[SEG_UDATA] = SEG32(STA_W,           0,     0xffffffff, DPL_USER);
  gdt[SEG_TSS]   = SEG16(STS_T32A,     &tss,  sizeof(tss)-1, DPL_KERN);
  set_gdt(gdt, sizeof(gdt[0]) * NR_SEG);
  set_tr(KSEL(SEG_TSS));
}

void set_tss(uint32_t ss0, uint32_t esp0) {
  tss.ss0 = ss0;
  tss.esp0 = esp0;
}

static PD kpd;
static PT kpt[PHY_MEM / PT_SIZE] __attribute__((used));

void init_page() {
  extern char end;
  panic_on((size_t)(&end) >= KER_MEM - PGSIZE, "Kernel too big (MLE)");
  static_assert(sizeof(PTE) == 4, "PTE must be 4 bytes");
  static_assert(sizeof(PDE) == 4, "PDE must be 4 bytes");
  static_assert(sizeof(PT) == PGSIZE, "PT must be one page");
  static_assert(sizeof(PD) == PGSIZE, "PD must be one page");
  // Lab1-4: init kpd and kpt, identity mapping of [0 (or 4096), PHY_MEM)
  // TODO change here 3.11
  for(int i = 0;i < PHY_MEM / PT_SIZE;i++){
    kpd.pde[i].val = MAKE_PDE((uint32_t)&kpt[i], PTE_P | PTE_W);
    for(int j = 0;j < NR_PTE;j++){
      uint32_t virtual_addr = (i << DIR_SHIFT) | (j << TBL_SHIFT);
      kpt[i].pte[j].val = MAKE_PTE(virtual_addr, PTE_P | PTE_W);
    }
  }
  kpt[0].pte[0].val = 0;
  set_cr3(&kpd);
  set_cr0(get_cr0() | CR0_PG);
  // Lab1-4: init free memory at [KER_MEM, PHY_MEM), a heap for kernel
  // 计算空闲页的数量
  // 分配并初始化空闲页的数据结构
  free_page_list = (page_t *)KER_MEM;
  for (int i = 1; i < (PHY_MEM - KER_MEM) / PGSIZE; i++)
  {
    free_page_list->next = (page_t *)PAGE_DOWN(KER_MEM + PGSIZE * i);
    free_page_list = free_page_list->next;
  }
  free_page_list->next = NULL;
  free_page_list = (page_t *)KER_MEM;
  // printf("this is a ckeck");
}

void *kalloc() {
  // Lab1-4: alloc a page from kernel heap, abort when heap empty
  if(free_page_list == NULL){
    assert(0);
  }
  page_t *temp = free_page_list;
  free_page_list = free_page_list->next;
  // 不是哥们，这边还能错？？？
  memset(temp, 0, PGSIZE);
  return temp;
}

void kfree(void *ptr) {
  // Lab1-4: free a page to kernel heap
  // you can just do nothing :)
  page_t *temp = (page_t *)ptr;
  temp->next = free_page_list;
  free_page_list = temp;
}

PD *vm_alloc() {
  // Lab1-4: alloc a new pgdir, map memory under PHY_MEM identityly
  // 分配一页作为用户页目录
  PD *upd = (PD *)kalloc();

  // 将 [0, PHY_MEM) 进行恒等映射
  for (int i = 0; i < PHY_MEM / PT_SIZE;i++) {
    // 将前32个PDE映射到内核页表
    if (i < 32) {
      upd->pde[i].val = MAKE_PDE(&kpt[i], PTE_P);
      continue;
    }
    upd->pde[i].val = 0;
  }
  return upd;
}

void vm_teardown(PD *pgdir) {
}

PD *vm_curr() {
  return (PD*)PAGE_DOWN(get_cr3());
}

PTE *vm_walkpte(PD *pgdir, size_t va, int prot) {
  // Lab1-4: return the pointer of PTE which match va
  // if not exist (PDE of va is empty) and prot&1, alloc PT and fill the PDE
  // if not exist (PDE of va is empty) and !(prot&1), return NULL
  // remember to let pde's prot |= prot, but not pte
  assert((prot & ~7) == 0);
  int pd_index = ADDR2DIR(va);
  PDE *pde = &(pgdir->pde[pd_index]);
  // 检查PDE的有效位
  if (!pde->present) {
    // 如果PDE不存在
    if (prot == 0) {
      // prot为0，直接返回NULL
      return NULL;
    } else {
      // 分配一页作为页表，并清零
      PT *pt = (PT *)kalloc();
      memset(pt, 0, PGSIZE);
      // 设置PDE的权限和页表地址
      pde->val = MAKE_PDE(pt, prot);
      // 返回指向对应PTE的指针
      return &(pt->pte[ADDR2TBL(va)]);
    }
  } else {
    // PDE存在，找到对应的页表项
    pde->val = pde->val | prot;
    PT *pt = PDE2PT(*pde);
    return &(pt->pte[ADDR2TBL(va)]);
  }
}

void *vm_walk(PD *pgdir, size_t va, int prot) {
  // Lab1-4: translate va to pa
  // if prot&1 and prot voilation ((pte->val & prot & 7) != prot), call vm_pgfault
  // if va is not mapped and !(prot&1), return NULL
  PTE *pte = vm_walkpte(pgdir, va, prot);
  if (pte == NULL) {
    if (!(prot & 1)) {
      return NULL;
    }
    // Call vm_pgfault if prot violation
    pte = vm_walkpte(pgdir, va, prot);
  }
  void *page = PTE2PG(*pte);
  void *pa = (void *)((uintptr_t)page | ADDR2OFF(va));
  return pa;
}

void vm_map(PD *pgdir, size_t va, size_t len, int prot) {
  // Lab1-4: map [PAGE_DOWN(va), PAGE_UP(va+len)) at pgdir, with prot
  // if have already mapped pages, just let pte->prot |= prot
  assert(prot & PTE_P);
  assert((prot & ~7) == 0);
  size_t start = PAGE_DOWN(va);
  size_t end = PAGE_UP(va + len);
  assert(start >= PHY_MEM);
  assert(end >= start);
  // TODO();
  for (size_t addr = start; addr < end; addr += PGSIZE) {
    PT *pt = (PT *)kalloc();
    PTE *pte = vm_walkpte(pgdir, addr, prot);
    pte->val = MAKE_PTE(pt, prot);
  }
}

void vm_unmap(PD *pgdir, size_t va, size_t len) {
  // Lab1-4: unmap and free [va, va+len) at pgdir
  // you can just do nothing :)
  //assert(ADDR2OFF(va) == 0);
  //assert(ADDR2OFF(len) == 0);
  //TODO();
}

void vm_copycurr(PD *pgdir) {
  // Lab2-2: copy memory mapped in curr pd to pgdir
  // TODO();
  uintptr_t start_va = PHY_MEM;
  uintptr_t end_va = USR_MEM;
    for (size_t va = start_va; va < end_va; va += PGSIZE) {
        PTE *pte = vm_walkpte(vm_curr(), va, 0);
        if (pte != NULL && pte->present) {
            // Page is mapped in current page directory, copy it to pgdir
            int perm =( pte->val )& (0x7);
            // Map the virtual page in pgdir
            vm_map(pgdir, (size_t)va, PGSIZE, perm);
            // Copy the contents of the original virtual page to the new physical page
            void *pa = vm_walk(pgdir,(size_t)va,perm);
            memcpy(pa, (void*)va, PGSIZE);
        }
    }
}

void vm_pgfault(size_t va, int errcode) {
  printf("pagefault @ 0x%p, errcode = %d\n", va, errcode);
  panic("pgfault");
}
