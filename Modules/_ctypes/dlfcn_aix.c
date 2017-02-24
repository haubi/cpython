/* Allow for loading multiple shared objects from single archive at once:
   AIX libc.a provides multiple shared members for multiple purpose other
   than just versioning. As portable python programs should not know about
   archive members, but want to dlopen the C-library as a whole, they use
   the ctypes.util.find_library() for the platform specific library name,
   to pass to _ctypes.dlopen(), probably via ctypes.cdll.
   So we support to dlopen "filename(member,member,...)" here. */

#include "ctypes_dlfcn.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

typedef int (*DlCloseFunc)(void *);
typedef void *(*DlSymFunc)(void *, const char *);

typedef struct {
    DlCloseFunc dl_close;
    DlSymFunc   dl_sym;
} Loader;

typedef struct {
    const Loader * loader;
} ObjectBase;

typedef struct {
    const Loader * loader;
    void * dlhandle;
} SingleObject;
    
static int single_close(void * handle)
{
    if (handle) {
        SingleObject *object = (SingleObject*)handle;
        int ret = dlclose(object->dlhandle);
        free(handle);
        return ret;
    }
    errno = EINVAL;
    return EINVAL;
}

static void *single_sym(void * handle, const char * symbol)
{
    if (handle) {
        SingleObject * object = (SingleObject*)handle;
        return dlsym(object->dlhandle, symbol);
    }
    errno = EINVAL;
    return NULL;
}

static void * single_open(const char * path, int mode)
{
    static const Loader singleLoader = { single_close, single_sym };
    SingleObject * object = (SingleObject*)malloc(sizeof(SingleObject));
    if (object) {
        object->dlhandle = dlopen(path, mode);
        if (object->dlhandle) {
            object->loader = &singleLoader;
            return (void*)object;
        }
        free(object);
    }
    return NULL;
}

typedef struct {
    const Loader * loader;
    void * dlhandles[1];
} MultipleObjects;

static int multiple_close(void * handle)
{
    if (handle) {
        int ret = 0;
        MultipleObjects * objects = (MultipleObjects*)handle;
        void ** dlhandles = objects->dlhandles;
        while(*dlhandles) {
            if (dlclose(*dlhandles) != 0)
                ret = EINVAL;
            ++dlhandles;
        }
        free(objects);
        if (ret == 0) /* each successfully dlclosed */
            return ret;
    }
    errno = EINVAL;
    return EINVAL;
}

static void * multiple_sym(void * handle, const char * symbol)
{
    if (handle) {
        MultipleObjects * objects = (MultipleObjects*)handle;
        void ** dlhandles = objects->dlhandles;
        while(*dlhandles) {
            void * ret = dlsym(*dlhandles, symbol);
            if (ret || errno)
                return ret;
            ++dlhandles;
        }
        return NULL;
    }
    errno = EINVAL;
    return NULL;
}

static void * multiple_open(const char *path, const char * members, int mode)
{
    static const Loader multipleLoader = { multiple_close, multiple_sym };
    MultipleObjects * loaded = NULL;
    char * pathbuf = strdup(path);
    if (pathbuf) {
        char * memberbuf = members - path + pathbuf;
        int memberno = 1;
        int i;
        for(i = 0; members[i]; ++i) {
            if (members[i] == ',') {
                ++memberno;
            }
        }
        loaded = (MultipleObjects*)malloc(sizeof(MultipleObjects) +
                                          memberno * sizeof(void*));
        if (loaded) {
            loaded->loader = &multipleLoader;
            memberno = 0;
            while(members) {
                const char * nextmember = strchr(members, ',');
                if (!nextmember)
                    nextmember = members + strlen(members) - 1;
                strncpy(memberbuf, members, nextmember - members);
                strcpy(nextmember - members + memberbuf, ")");
                loaded->dlhandles[memberno] = dlopen(pathbuf, mode);
                if (!loaded->dlhandles[memberno])
                    break;
		loaded->dlhandles[++memberno] = NULL;
                members = (*nextmember == ',') ? nextmember+1 : NULL;
            }
            if (members) {
                multiple_close(loaded);
                loaded = NULL;
            }
        }
    }
    free(pathbuf);
    return loaded;
}

void * ctypes_dlopen(const char *path, int mode)
{
    if (path != NULL &&
        path != RTLD_DEFAULT &&
        path != RTLD_MYSELF &&
        path != RTLD_NEXT) {
        /* multiple member spec is "filename(member,member...)" */
        const char * closing = strrchr(path, ')');
        if (closing && strlen(closing) == 1) {
            const char * opening = strrchr(path, '(');
            if (opening > path &&        /* not NULL and len(filename) > 0 */
                strlen(opening) > 2 &&   /* len(member) > 0 */
                !strchr(opening, '/')) { /* no pathsep in member */
                mode |= RTLD_MEMBER;
                if (strchr(opening, ',')) { /* multiple members */
                    return multiple_open(path, opening + 1, mode);
                }
            }
        }
    }
    return single_open(path, mode);
}

int ctypes_dlclose(void * handle)
{
    if (handle != NULL &&
        handle != RTLD_DEFAULT &&
        handle != RTLD_MYSELF &&
        handle != RTLD_NEXT) {
        ObjectBase * object = (ObjectBase*)handle;
        return object->loader->dl_close(handle);
    }
    return dlclose(handle);
}

void * ctypes_dlsym(void * handle, const char * symbol)
{
    if (handle != NULL &&
        handle != RTLD_DEFAULT &&
        handle != RTLD_MYSELF &&
        handle != RTLD_NEXT) {
        ObjectBase * object = (ObjectBase*)handle;
        return object->loader->dl_sym(handle, symbol);
    }
    return dlsym(handle, symbol);
}
