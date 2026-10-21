#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/init.h"
#include "threads/synch.h"
#include "threads/thread.h"

struct process_list {
  struct list list;
  struct lock lock;
};

struct process_desc {
  tid_t tid;
  tid_t ppid;
  int exit_status;
  struct semaphore semaphore;
  struct list_elem elem;
};

// enum sema_op { SEMA_UP, SEMA_DOWN };

void process_list_init(void);
struct process_desc *process_desc_create(tid_t tid, tid_t ppid);
void process_desc_add(struct process_desc *p);
void process_desc_remove(struct process_desc *p);
struct process_desc *process_desc_find(tid_t tid);
void process_kill_children(tid_t tid);
void process_desc_exit (int status);
int  process_desc_wait (tid_t child_tid);

// int process_desc_sema (tid_t tid, enum sema_op op, int status);

tid_t process_create_initd (const char *file_name);
tid_t process_fork (const char *name, struct intr_frame *if_);
int process_exec (void *f_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (struct thread *next);

#endif /* userprog/process.h */
