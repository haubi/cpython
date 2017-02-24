#ifndef _DLFCN_AIX_H
#define _DLFCN_AIX_H

#ifdef _AIX

#ifdef __cplusplus
extern "C" {
#endif

extern void * ctypes_dlopen(const char *path, int mode);
extern void * ctypes_dlsym(void * handle, const char * symbol);
#define ctypes_dlerror dlerror
extern int ctypes_dlclose(void * handle);

#ifdef __cplusplus
}
#endif

#endif /* _AIX */

#endif /* _DLFCN_AIX_H */
