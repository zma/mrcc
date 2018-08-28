// mrcc - A C Compiler system on MapReduce
// Zhiqiang Ma, https://www.ericzma.com

#ifndef _HEADER_FILES_H
# define _HEADER_FILES_H


char* get_basename_no_ext(char* sfile);
const char * find_basename(const char *sfile);
char * find_extension(char *sfile);
const char * find_extension_const(const char *sfile);
int is_source(const char *sfile);
int is_object(const char *filename);
int is_preprocessed(const char *sfile);
int source_needs_local(const char *filename);
int output_from_source(const char *sfile, const char *out_extn, char **ofile);

const char * preproc_exten(const char *e);


#endif //_HEADER_FILES_H
