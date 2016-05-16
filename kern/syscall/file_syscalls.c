#include <syscall.h>
#include <proc.h>
#include <vnode.h>
#include <kern/errno.h>
#include <kern/fcntl.h>
#include <vfs.h>
#include <current.h>
#include <uio.h>
#include <kern/iovec.h>

char* int_to_byte_string(int input)
{
  char* digits = kmalloc(21);
  if (digits == NULL)
  {
    return NULL;
  }
  int digitcount = 0;
  while(input > 0)
  {
    digits[digitcount] = input % 256;
    input /= 256;
    digitcount++;
  }
  digits[digitcount] = 0;
  return digits;
}

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
  char* fdkey = int_to_byte_string(fd);
  int addresult = proc_addfile(cur, fdkey, ctrl);
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
  char* fdkey = int_to_byte_string(fd);
  fcblock *ctrl = (fcblock*) hashtable_find(cur->files, fdkey, strlen(fdkey));
  kfree(fdkey);
  if(ctrl == NULL)
  {
    if (fd < 3)
    {
      int last_fd = cur->next_fd;
      cur->next_fd = 0;
      sys_open("con:", O_RDWR, error);
      sys_open("con:", O_RDWR, error);
      sys_open("con:", O_RDWR, error);
      cur->next_fd = last_fd;
      fdkey = int_to_byte_string(fd);
      ctrl = (fcblock*) hashtable_find(cur->files, fdkey, strlen(fdkey));
      kfree(fdkey);
    }
    else
    {
      *error = EBADF;
      return -1;
    }
  }
  if(ctrl->permissions != O_RDONLY && ctrl->permissions != O_RDWR)
  {
    *error = EBADF;
    return -1;
  }
  struct iovec iov;
  struct uio reader;
  uio_kinit(&iov, &reader, buf, buflen, ctrl->offset, UIO_READ);
  int vop_result = VOP_READ(ctrl->node, &reader);
  if (vop_result != 0)
  {
    *error = EIO;
    return -1;
  }
  int result = reader.uio_offset - ctrl->offset;
  ctrl->offset = reader.uio_offset;
  return result;
}

ssize_t sys_write(int fd, const void *buf, size_t nbytes, int* error)
{
  struct proc *cur = curproc;
  
  if(cur == NULL)
  {
		panic("User processes must always have a process control block.");
	}
  char* fdkey = int_to_byte_string(fd);
  fcblock *ctrl = (fcblock*) hashtable_find(cur->files, fdkey, strlen(fdkey));
  kfree(fdkey);
  if(ctrl == NULL)
  {
    if (fd < 3)
    {
      int last_fd = cur->next_fd;
      cur->next_fd = 0;
      sys_open("con:", O_RDWR, error);
      sys_open("con:", O_RDWR, error);
      sys_open("con:", O_RDWR, error);
      cur->next_fd = last_fd;
      fdkey = int_to_byte_string(fd);
      ctrl = (fcblock*) hashtable_find(cur->files, fdkey, strlen(fdkey));
      kfree(fdkey);
    }
    else
    {
      *error = EBADF;
      return -1;
    }
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
  int vop_result = VOP_WRITE(ctrl->node, &writer);
  if (vop_result != 0)
  {
    *error = EIO;
    return -1;
  }
  int result = writer.uio_offset - ctrl->offset;
  ctrl->offset = writer.uio_offset;
  kfree(bufcpy);
  return result;
}

int sys_close(int fd, int* error)
{
  struct proc *cur = curproc;
  if(cur == NULL){
		panic("User processes must always have a process control block.");
	}
  char* fdkey = int_to_byte_string(fd);
  fcblock *ctrl = (fcblock*) hashtable_remove(cur->files, fdkey, strlen(fdkey));
  kfree(fdkey);
  if (ctrl == NULL)
  {
    *error = EBADF;
    return -1;
  }
  vfs_close(ctrl->node);
  kfree(ctrl);
  return 0;
}

