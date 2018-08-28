// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_TEMP_FILE_H
# define _HEADER_TEMP_FILE_H

int get_tmp_top(const char **p_ret);

int make_tmpnam(const char *prefix, const char *suffix, char **name_ret);

int get_top_dir(char **path_ret);

int mrcc_mkdir(const char *path);

int get_subdir(const char *name, char **dir_ret);


int get_lock_dir(char **dir_ret);


int get_state_dir(char **dir_ret);


#endif //_HEADER_TEMP_FILE_H
