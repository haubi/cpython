#include "Python.h"

#include <dlfcn.h> /* for RTLD_MEMBER */
#include <sys/ldr.h> /* for loadquery */

PyDoc_STRVAR(loadquery__doc__,
"loadquery(integer) -> list\n\n"
"Please find details in the AIX loadquery(2) man page.");

static PyObject *
py_loadquery(PyObject *self, PyObject *args)
{
    PyObject * final_result = NULL;
    PyObject * result;
    PyObject * entry = NULL;
    int flags;
    void * ldibuf = NULL;
    int errflag, bufsize = 1024;

    if (!PyArg_ParseTuple(args, "i:loadquery", &flags))
        return NULL;

    switch (flags) {
    case L_GETINFO:
    case L_GETMESSAGES:
    case L_GETLIBPATH:
    case L_GETXINFO:
        break;
    default:
        PyErr_SetString(PyExc_ValueError, strerror(EINVAL));
        return NULL;
    }

    do {
        ldibuf = malloc(bufsize);
        while (ldibuf != NULL &&
               (errflag = loadquery(flags, ldibuf, bufsize)) == -1 &&
               errno == ENOMEM) {
            free(ldibuf);
            bufsize += 1024;
            ldibuf = malloc(bufsize);
        }
        if (ldibuf == NULL) {
            PyErr_SetString(PyExc_MemoryError, strerror(errno));
            break;
        }
        if (errflag == -1) {
            PyErr_SetString(PyExc_SystemError, strerror(errno));
            break;
        }

        result = PyList_New(0);
        if (!result)
            break;

        if (flags == L_GETMESSAGES) {
            char const **pmsg = (char const **)ldibuf;

            while(*pmsg) {
                entry = Py_BuildValue("s", *pmsg);
                if (!entry)
                    break;
                if (PyList_Append(result, entry) < 0)
                    break;
                Py_CLEAR(entry);
                ++pmsg;
            }

            if (*pmsg)
                break;
        }
        else if (flags == L_GETLIBPATH) {
            char * libpath = (char*)ldibuf;

            while(*libpath) {
                char * next = strchr(libpath, ':');
                if (!next)
                    next = libpath + strlen(libpath);
                entry = Py_BuildValue("s#", libpath, next - libpath);
                if (!entry)
                    break;
                if (PyList_Append(result, entry) < 0)
                    break;
                Py_CLEAR(entry);
                libpath = *next ? next+1 : next;
            }

            if (*libpath)
                break;
        }
        else if (flags == L_GETINFO) {
            struct ld_info *ldi = NULL;

            do {
                char const * filename;
                char const * member;

                ldi = (struct ld_info*)
                    (ldi ? ldi->ldinfo_next + (void*)ldi : ldibuf);

                filename = ldi->ldinfo_filename;
                member = filename + strlen(filename) + 1;
                if (!*member)
                    member = NULL;

                entry = Py_BuildValue("{ssss}",
                            "filename", filename,
                            "member", member);
                if (!entry)
                    break;
                if (PyList_Append(result, entry) < 0)
                    break;
                Py_CLEAR(entry);
            } while(ldi->ldinfo_next);

            if (ldi->ldinfo_next)
                break;
        }
        else if (flags == L_GETXINFO) {
            struct ld_xinfo *ldxi = NULL;

            do {
                char const * filename;
                char const * member;

                ldxi = (struct ld_xinfo*)
                    (ldxi ? ldxi->ldinfo_next + (void*)ldxi : ldibuf);

                filename = ldxi->ldinfo_filename + (void*)ldxi;
                member = filename + strlen(filename) + 1;
                if (!*member)
                    member = NULL;

                entry = Py_BuildValue("{sssssksksksksI}",
                            "filename", filename,
                            "member", member,
                            "textsize", (unsigned long)ldxi->ldinfo_textsize,
                            "datasize", (unsigned long)ldxi->ldinfo_datasize,
                            "tdatasize", (unsigned long)ldxi->ldinfo_tdatasize,
                            "tdataoff", (unsigned long)ldxi->ldinfo_tdataoff,
                            "tls_rnum", (unsigned long)ldxi->ldinfo_tls_rnum);
                if (!entry)
                    break;
                if (PyList_Append(result, entry) < 0)
                    break;
                Py_CLEAR(entry);
            } while(ldxi->ldinfo_next);

            if (ldxi->ldinfo_next)
                break;
        }

        final_result = result;
    } while(0);

    if (!final_result && result)
        Py_DECREF(result);

    free(ldibuf);

    return final_result;
}

static PyMethodDef _ctypes_aix_module_methods[] = {
    {"loadquery", py_loadquery, METH_VARARGS, loadquery__doc__},
    {NULL,        NULL}         /* Sentinel */
};

PyMODINIT_FUNC
init_ctypes_aix(void)
{
    PyObject *m;

    m = Py_InitModule("_ctypes_aix", _ctypes_aix_module_methods);
    if (!m)
        return;

    /* additional flags for _ctypes.dlopen */
    PyModule_AddObject(m, "RTLD_MEMBER", PyInt_FromLong(RTLD_MEMBER));

    /* flags for _ctypes_aix.loadquery */
    PyModule_AddObject(m, "L_GETINFO", PyInt_FromLong(L_GETINFO));
    PyModule_AddObject(m, "L_GETMESSAGES", PyInt_FromLong(L_GETMESSAGES));
    PyModule_AddObject(m, "L_GETLIBPATH", PyInt_FromLong(L_GETLIBPATH));
    PyModule_AddObject(m, "L_GETXINFO", PyInt_FromLong(L_GETXINFO));
}
