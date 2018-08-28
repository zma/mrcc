// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_CLEAN_UP_H
# define _HEADER_CLEAN_UP_H


int add_cleanup(const char *filename);

void cleanup_tempfiles_from_signal_handler(void);

void cleanup_tempfiles(void);

#endif //_HEADER_CLEAN_UP_H
