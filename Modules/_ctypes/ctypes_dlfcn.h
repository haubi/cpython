#ifndef _CTYPES_DLFCN_H_
#define _CTYPES_DLFCN_H_

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifndef MS_WIN32

#include <dlfcn.h>

#ifdef CTYPES_DARWIN_DLFCN

/* found "darwin/dlfcn.h" above */

#elif defined(_AIX)

# include "dlfcn_aix.h"

#else

#define ctypes_dlsym dlsym
#define ctypes_dlerror dlerror
#define ctypes_dlopen dlopen
#define ctypes_dlclose dlclose
#define ctypes_dladdr dladdr

#endif /* !CTYPES_DARWIN_DLFCN */

#endif /* !MS_WIN32 */

#ifdef __cplusplus
}
#endif /* __cplusplus */
#endif /* _CTYPES_DLFCN_H_ */
