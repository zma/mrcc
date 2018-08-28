// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_IO_H
# define _HEADER_IO_H


#ifndef O_BINARY
#  define O_BINARY 0
#endif



int select_for_write(int fd, int timeout);
int writex(int fd, const void *buf, size_t len);
int mrcc_close(int fd);

int open_read(const char *fname, int *ifd, off_t *fsize);

int copy_file_to_fd(const char *in_fname, int out_fd);

#endif //_HEADER_IO_H
