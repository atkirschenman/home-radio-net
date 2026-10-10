/* No-op newlib syscalls: there is no file I/O on the nodes. Defining them here
 * silences libnosys's "not implemented" link warnings so real ones stand out. */
#include <errno.h>
#include <sys/stat.h>

int _close(int fd);
int _fstat(int fd, struct stat *st);
int _isatty(int fd);
int _lseek(int fd, int off, int whence);
int _read(int fd, char *buf, int len);
int _write(int fd, const char *buf, int len);

int _close(int fd) { (void)fd; errno = EBADF; return -1; }
int _fstat(int fd, struct stat *st) { (void)fd; st->st_mode = S_IFCHR; return 0; }
int _isatty(int fd) { (void)fd; return 1; }
int _lseek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int _read(int fd, char *buf, int len) { (void)fd; (void)buf; (void)len; errno = EBADF; return -1; }
int _write(int fd, const char *buf, int len) { (void)fd; (void)buf; return len; }
