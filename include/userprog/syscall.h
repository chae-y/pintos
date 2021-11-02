#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H
#include <stdbool.h>
#include <debug.h>
#include <stddef.h>

void syscall_init (void);

struct lock filesys_lock;


// TODO ============================ for Project 4 =================================
bool is_dir(int fd);
bool chdir(const char *path_name);
bool mkdir(const char *dir);
bool readdir(int fd, char *name);
struct cluster_t *inumber(int fd);

#endif /* userprog/syscall.h */
