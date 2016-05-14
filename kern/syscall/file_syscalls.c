#include <syscall.h>
#include <proc.h>
#include <vnode.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <current.h>
#include <uio.h>
#include <kern/iovec.h>

int sys_open(const char *filename, int flags, int* error)
{
  fcblock *ctrl = (fcblock*) 
        kmalloc(sizeof(fcblock));
  if (ctrl == NULL)
  {
    *error = ENOMEM;
    return -1;
  }
  char* name = kstrdup(filename);
  int vfsresult = vfs_open(name, flags, 0, &(ctrl->node));
  if (vfsresult != 0)
  {
    *error = vfsresult;
    return -1;
  }
  ctrl->offset = 0;
  ctrl->permissions = flags & O_ACCMODE;
  struct proc *cur = curproc;
  if(cur == NULL){
		panic("User processes must always have a process control block.");
	}
  int fd = cur->next_fd;
  int addresult = proc_addfile(cur, fd, ctrl);
  if (addresult != 0)
  {
    *error = addresult;
    return -1;
  }
  (cur->next_fd)++;
  return fd;
}

ssize_t sys_read(int fd, void *buf, size_t buflen, int* error)
{
  struct proc *cur = curproc;
  if(cur == NULL)
  {
		panic("User processes must always have a process control block.");
	}
  fcblock *ctrl = (fcblock*) hashtable_find(cur->files, (char *) fd, 1);
  if(ctrl == NULL)
  {
    *error = EBADF;
    return -1;
  }
  if(ctrl->permissions != O_RDONLY && ctrl->permissions != O_RDWR)
  {
    *error = EBADF;
    return -1;
  }
  struct iovec iov;
  struct uio reader;
  uio_kinit(&iov, &reader, buf, buflen, ctrl->offset, UIO_READ);
  ssize_t result = VOP_READ(ctrl->node, &reader);
  ctrl->offset += result;
  return result;
}

ssize_t sys_write(int fd, const void *buf, size_t nbytes, int* error)
{
  struct proc *cur = curproc;
  if(cur == NULL)
  {
		panic("User processes must always have a process control block.");
	}
  fcblock *ctrl = (fcblock*) hashtable_find(cur->files, (char *) fd, 1);
  if(ctrl == NULL)
  {
    *error = EBADF;
    return -1;
  }
  if(ctrl->permissions != O_WRONLY && ctrl->permissions != O_RDWR)
  {
    *error = EBADF;
    return -1;
  }
  void* bufcpy = kmalloc(nbytes);
  if (bufcpy == NULL)
  {
    *error = ENOMEM;
    return -1;
  }
  memcpy(bufcpy, buf, nbytes);
  struct iovec iov;
  struct uio writer;
  uio_kinit(&iov, &writer, bufcpy, nbytes, ctrl->offset, UIO_WRITE);
  ssize_t result = VOP_WRITE(ctrl->node, &writer);
  ctrl->offset += result;
  kfree(bufcpy);
  return result;
}

int sys_close(int fd)
{
  struct proc *cur = curproc;
  if(cur == NULL){
		panic("User processes must always have a process control block.");
	}
  fcblock *ctrl = (fcblock*) hashtable_remove(cur->files, (char *) fd, 1);
  if (ctrl == NULL)
  {
    return EBADF;
  }
  vfs_close(ctrl->node);
  kfree(ctrl);
  return 0;
}

