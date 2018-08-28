// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_NETFSUTILS_H
# define _HEADER_NETFSUTILS_H


// top dir of temp files in net fs
extern const char* fs_top_dir;
// output file suffix
extern const char* fs_out_file_suffix;
// output dir suffix
extern const char* fs_out_dir_suffix;

// the prefix for noting the file is on net fs when clean up
extern const char* net_file_prefix_for_clean_up;

char* name_local_cpp_to_local_outfile(char* cpp_fname);
char* name_local_cpp_to_local_outdir(char* cpp_fname);

int get_file_fs(char* srt, char* localdst);
int put_file_fs(char* localsrc, char* dst);
int del_file_fs(char* fname);
//int del_dir_fs(char* fname);

char* name_local_to_fs(char* localname);
char* name_fs_to_local(char* fsname);

int is_cleanup_on_fs(char* fname);
int cleanup_file_fs(char* fname);
int add_cleanup_fs(char* fname);

#endif //_HEADER_NETFSUTILS_H
