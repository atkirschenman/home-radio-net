#include <cerrno>
#include <sys/stat.h>

extern "C" {

int _close(int)
{
    errno = EBADF;
    return -1;
}

int _fstat(int, struct stat *status)
{
    status->st_mode = S_IFCHR;
    return 0;
}

int _isatty(int)
{
    return 1;
}

int _lseek(int, int, int)
{
    return 0;
}

int _read(int, char *, int)
{
    errno = EBADF;
    return -1;
}

int _write(int, const char *, int length)
{
    return length;
}

int _getpid()
{
    return 1;
}

int _kill(int, int)
{
    errno = EINVAL;
    return -1;
}

void __cxa_pure_virtual()
{
    for (;;) {
    }
}

} // extern "C"
