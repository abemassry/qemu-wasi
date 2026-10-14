#ifndef _WASI_SYS_WAIT_H_STUB
#define _WASI_SYS_WAIT_H_STUB
/* Stub sys/wait.h for WASI */
#define WIFEXITED(x)   1
#define WEXITSTATUS(x) (x)
#define WIFSIGNALED(x) 0
#define WTERMSIG(x)    0
#define WIFSTOPPED(x)  0
#define WSTOPSIG(x)    0
#define WNOHANG 1
#endif
