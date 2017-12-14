#include "userprog/syscall.h"
#include "userprog/process.h"
#include <stdio.h>
#include <syscall-nr.h>
#include <string.h>
#include "threads/thread.h"
#include "threads/init.h"
#include "threads/malloc.h"

#include "filesys/file.h"
#include "filesys/inode.h"
#include "devices/input.h"
#include "devices/shutdown.h"

#define checkARG 	if((uint32_t)esp > 0xc0000000-(argsNum+1)*4) \
  syscall_exit(f,argsNum);

static void syscall_handler (struct intr_frame *);
static struct list fd_list;

struct lock FILELOCK;

int currentFd(struct thread *cur, bool isdir)
{
  struct list_elem *e = list_begin(&fd_list);
  int result = 0;
  for(;e!=list_end(&fd_list);e=list_next(e))
  {
    struct fd_elem *fe = list_entry(e,struct fd_elem, elem);
    if(fe->owner == cur) {
      if((!isdir && fe->fd % 2 == 1) || (isdir && fe->fd % 2 == 0))
        result = fe->fd;
    }
  }
  if(result == 0)
  {
    if(!isdir)
      result = 3;
    else
      result = 4;
  }
  return result;
}

void* getFile(int fd, struct thread *cur)
{
  void* result = NULL;
  struct fd_elem *fe =  getFD_elem(fd, cur);
  if(fd % 2 == 1) {
    result = fe->file;
    if(fe->isEXE)
      file_deny_write(fe->file);
  }
  else
    result = fe->dir;
   
  return result;
}

struct fd_elem* getFD_elem(int fd, struct thread *cur)
{
  struct list_elem *e = list_begin(&fd_list);
  for(;e!=list_end(&fd_list);e=list_next(e))
  {
    struct fd_elem *fe = list_entry(e,struct fd_elem, elem);
    if(fe->owner == cur && fe->fd == fd)
      return fe;
  }
  return NULL; 
}

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
  list_init(&fd_list);
  lock_init(&FILELOCK);
}

static void
syscall_handler (struct intr_frame *f) 
{
  uint32_t syscall_num = *(uint32_t *)(f->esp);

  switch(syscall_num){
    case SYS_HALT: syscall_halt(f);           /* Halt the operating system. */
        break;
    case SYS_EXIT: syscall_exit(f,1);         /* Terminate this process. */
	break;
    case SYS_EXEC: syscall_exec(f,1);         /* Start another process. */
	break;
    case SYS_WAIT: syscall_wait(f,1);         /* Wait for a child process to die. */
	break;
    case SYS_CREATE: syscall_create(f,2);     /* Create a file. */
	break;
    case SYS_REMOVE: syscall_remove(f,1);     /* Delete a file. */
	break;
    case SYS_OPEN: syscall_open(f,1);         /* Open a file. */
	break;
    case SYS_FILESIZE: syscall_filesize(f,1); /* Obtain a file's size. */
	break;
    case SYS_READ:  syscall_read(f,3);        /* Read from a file. */
	break;
    case SYS_WRITE: syscall_write(f,3);       /* Write to a file. */
	break;
    case SYS_SEEK: syscall_seek(f,2);         /* Change position in a file. */
	break;
    case SYS_TELL: syscall_tell(f,1);        
	break;
    case SYS_CLOSE: syscall_close(f,1);      
	break;
    case SYS_CHDIR: syscall_chdir(f, 1);
        break;
    case SYS_MKDIR: syscall_mkdir(f, 1);
        break;
    case SYS_READDIR: syscall_readdir(f, 2);
        break;
    case SYS_ISDIR: syscall_isdir(f, 1);
        break;
    case SYS_INUMBER: syscall_inumber(f, 1);
        break;

  }	
}


void syscall_halt(struct intr_frame *f UNUSED)
{
  shutdown_power_off();
}

void allClose(struct thread *cur)
{
  struct list_elem *e = list_begin(&fd_list);
  for(;e!=list_end(&fd_list);e=list_next(e))
  {
    struct fd_elem *fe = list_entry(e,struct fd_elem, elem);
    if(fe->owner == cur)
    {
      if(fe->fd % 2 == 1) 
        file_close(fe->file);
      else  
        dir_close(fe->dir);
      list_remove(e);	
      e=list_prev(e);
      free(fe);
    }
  }
}

void syscall_exit(struct intr_frame *f,int argsNum)
{
  void *esp = f->esp;
  struct thread *cur = thread_current(); 
  struct child_info *ci = getCIFromTid(cur->tid);
  int status;

  if(argsNum != -1)		// by kernel
  {
    if((uint32_t)esp > 0xc0000000-(argsNum+1)*4){					// bad arg address
      status = -1;
    } else {
      status = *(int *)(esp+4);
    }
  } else status = -1;

  if(ci != NULL){
    ci->exitCode = status;
  }
  if(FILELOCK.holder != cur)
    lock_acquire(&FILELOCK);
  allClose(cur);
  if(FILELOCK.holder == cur)
    lock_release(&FILELOCK);

  printf("%s: exit(%d)\n",cur->name,status);
  thread_exit();
}

void syscall_exec(struct intr_frame *f,int argsNum)
{
  void *esp = f->esp;

  checkARG

  char* command_line = *(char**)(esp+4);

  char buf[16];
  char *ptrptr;
  strlcpy(buf,command_line,16);
  strtok_r(buf," ",&ptrptr);

  if(filesys_open(buf) == NULL)
  {
    f->eax = -1;
    return;
  }

  tid_t tid = process_execute(command_line);	

  if(tid == TID_ERROR)
  {
    f->eax = -1;
    return;
  }

  struct child_info * ci = getCIFromTid(tid);

  sema_down(&ci->e_sema);

  if(ci->loadFail)
  {
    f->eax = -1;	
    return;
  }

  f->eax = tid;
  return;
}

void syscall_wait(struct intr_frame *f,int argsNum)
{
  void *esp = f->esp;

  checkARG

  tid_t tid = *(tid_t *)(esp+4);

  f->eax = process_wait(tid);
}

void syscall_create(struct intr_frame *f,int argsNum){
  void*esp = f->esp;
  checkARG

  char* file = *(char **)(esp+4);
  uint32_t initial_size = *(uint32_t *)(esp+8);
  if(strlen(file) <= 0)
  {
    f->eax = 0;
    return;
  } 
 
  lock_acquire(&FILELOCK);
  bool result = filesys_create(file, initial_size, false);
  lock_release(&FILELOCK);
  f->eax = (int)result;
}

void syscall_remove(struct intr_frame *f,int argsNum){

  void*esp = f->esp;
  checkARG

  char* file = *(char **)(esp+4);

  lock_acquire(&FILELOCK);
  bool result = filesys_remove(file);
  lock_release(&FILELOCK);
  f->eax = (int)result;

}

void syscall_open(struct intr_frame *f,int argsNum){

  void*esp = f->esp;
  checkARG

  char* filename = *(char **)(esp+4);

  if(filename == NULL){
    f->eax = -1;
    return;
  }

  struct thread *cur = thread_current();

  lock_acquire(&FILELOCK);
  struct file* file = filesys_open(filename);
  lock_release(&FILELOCK);
  if(file != NULL){
    struct fd_elem *fe = (struct fd_elem *)malloc(sizeof(struct fd_elem));
    fe->owner = cur;
    if(!inode_is_dir(file_get_inode(file))) {
      fe->file = file;
      fe->dir = NULL;
      fe->fd = currentFd(fe->owner, false) + 2;
      fe->filename = filename;
    
      if(checkIsThread(filename))
      {
        fe->isEXE = true;
      } else fe->isEXE = false;
    }
    else {
      fe->file = NULL;
      fe->dir = (struct dir *)file;
      fe->fd = currentFd(fe->owner, true) + 2;
    }
    list_push_back(&fd_list,&fe->elem);
    f->eax = fe->fd;
  } else f->eax = -1;
}

void syscall_filesize(struct intr_frame *f,int argsNum){

  void*esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);

  struct file *file = getFile(fd,thread_current());
  if(file != NULL)
    f->eax = file_length(file);
  else f->eax = -1;
}

void syscall_read(struct intr_frame *f,int argsNum){

  void*esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);
  char* buffer = *(char **)(esp+8);
  uint32_t size = *(uint32_t *)(esp+12);

  if(buffer>(char*)0xc0000000) syscall_exit(f,-1);
  if(fd == 0){
    uint32_t i;
    for(i = 0; i < size; i++)
    {
      buffer[i] = input_getc();		
    }
    f->eax = size;
  } else if(fd == 1 || fd % 2 == 0){
    f->eax = -1;
  } else {
    struct file *file = getFile(fd,thread_current());
    if (file != NULL)
    {
      if(file_tell(file) >= file_length(file))
	f->eax = 0;
      else f->eax = file_read(file,buffer,size);
    }
    else f->eax = -1;
  }
}

void syscall_write (struct intr_frame *f,int argsNum)
{
  void* esp = f->esp;

  checkARG

  int fd = *(int *)(esp+4);
  char* buffer = *(char **)(esp+8);
  uint32_t size = *(uint32_t *)(esp+12);
  if (fd == 1)
  {
    putbuf((char *)buffer,size);
    f->eax = size;
  } else if (fd == 0 || fd % 2 == 0){
    f->eax = -1;
  } else {
    struct file *file = getFile(fd,thread_current());
    if (file != NULL) {
      lock_acquire(&FILELOCK);
      f->eax = file_write(file,buffer,size);
      lock_release(&FILELOCK);
    }
    else f->eax = -1;
  }
}

void syscall_seek(struct intr_frame *f,int argsNum){
  void*esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);
  uint32_t position = *(uint32_t *)(esp+8);

  lock_acquire(&FILELOCK);
  struct file *file = getFile(fd,thread_current());
  if(file != NULL)
    file_seek(file,position);		

  lock_release(&FILELOCK);
}

void syscall_tell(struct intr_frame *f,int argsNum){
  void*esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);

  struct file *file = getFile(fd,thread_current());
  if(file != NULL)
  {
    f->eax = file_tell(file);
  } else f->eax = -1;
}

void syscall_close(struct intr_frame *f,int argsNum){
  void*esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);

  struct thread* cur = thread_current();
  struct fd_elem *fe = getFD_elem(fd, cur);
  if(fe->fd %2 == 1)
  {
    struct file* file = fe->file;
    if(file != NULL)
    {
      lock_acquire(&FILELOCK);
      file_close(file);
      lock_release(&FILELOCK);
      list_remove(&fe->elem); 
      free(fe); 
    }
  }
  else
  {
    struct dir* dir = fe->dir;
    if(dir != NULL) {
      dir_close(dir); 
      list_remove(&fe->elem); 
      free(fe);
    } 
  }
}


void syscall_chdir (struct intr_frame *f, int argsNum) 
{
  void* esp = f->esp;
  checkARG

  const char* dir = *(const char **)(esp+4);

  f->eax = filesys_chdir(dir);
}

void syscall_mkdir (struct intr_frame *f, int argsNum)
{ 
  void* esp = f->esp;
  checkARG

  const char* dir = *(const char **)(esp+4);
  f->eax = filesys_create(dir, 0, true);
}

void syscall_readdir (struct intr_frame *f, int argsNum)
{
  void* esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);
  char* name = *(char **)(esp+8);

  struct dir *dir = getFile(fd, thread_current());
  if (!dir) {
    f->eax = false;
    return;
  }
  if (!dir_readdir(dir, name)) {
    f->eax = false;
    return;
  }
  f->eax = true;
  return;
}

void syscall_isdir (struct intr_frame *f, int argsNum) 
{
  void* esp = f->esp;
  checkARG
  int fd = *(int *)(esp+4);

  struct fd_elem *fe = getFD_elem(fd, thread_current());
  if(fe->fd % 2 == 0)
    f->eax = true;
  else 
    f->eax = false;
}

void syscall_inumber (struct intr_frame *f, int argsNum)
{
  void* esp = f->esp;
  checkARG

  int fd = *(int *)(esp+4);

  struct fd_elem *fe = getFD_elem(fd, thread_current());
  if (!fe)
    {
      f->eax = -1;
      return;
    }
  block_sector_t inumber = -1;
  if (fe->fd % 2 == 0)
    inumber = inode_get_inumber(dir_get_inode(fe->dir));
  else 
    inumber = inode_get_inumber(file_get_inode(fe->file));

  f->eax = inumber;
}

