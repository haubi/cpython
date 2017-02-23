"""Internal platform implementation of find_library() for AIX.
Lib/ctype support for LoadLibrary interface to dlopen() for AIX
s implemented as a separate file, similiar to Darwin support ctype.macholib.*
rather than as separate sections in utils.py for improved
isolation and (I hope) readability

dlopen() is implemented on AIX as initAndLoad() - official documentation at:
https://www.ibm.com/support/knowledgecenter/ssw_aix_53/\
com.ibm.aix.basetechref/doc/basetrf1/dlopen.htm?lang=en and
https://www.ibm.com/support/knowledgecenter/ssw_aix_53/\
com.ibm.aix.basetechref/doc/basetrf1/load.htm?lang=en
"""
# Author: M Felt, aixtools.net, 2016
# Author: M Haubenwallner, ssi-schaefer.com, 2017
# Thanks to Martin Panter for his patience and comments.

import os
import re
import sys
import subprocess

def dump_objectfile(filename, **kwargs):
    """dump_objectfile(filename, symbols=False)
        -> list of dictionaries defining
            {"member": membername or None,
             "flags": set(flags),
             "symbols": set(symbols) }

    For each object with the DYNLOAD flag found along 'filename',
    each list entry is a dictionary defining "member", "flags"
    and "symbols".
    For the standalone object file, "member" value is None.

    /usr/bin/dump -THov output provides info on archive/executable
    contents and related paths to shareable members.
    """

    def aixABI():
        """aixABI() -> integer

        Returns current executable's bitwidth: 32 or 64.
        """
        if (sys.maxsize < 2**32):
            return 32
        return 64

# /usr/bin/dump flags
#  -p  Suppresses header printing.
#  -v  Dumps the information in symbolic representation rather than numeric.
#  -o  Dumps each optional header.
#  -T  Dumps the symbol table entries for the loader section.

    dumpopts = '-pvo'
    if kwargs.get('symbols', False):
        dumpopts += 'T'

    # The subprocess calls dump -Hov without shell assistence.
    p = subprocess.Popen(
            ["/usr/bin/dump", "-X%s"%aixABI(), dumpopts, filename],
            universal_newlines=True, stdout=subprocess.PIPE, shell=False)

    #dumpHov parsing:
    # Another file (standalone or archive member) starts when
    # 1. line is "filename:" - immediate object name,
    # 2. line is "file[*]:" - object as archive[member] name.
    # Subsequent lines define properties for that object. When
    # 1. line is "Flags=( * )" - the flags for that object file.

    objects = []
    obj = None

    for line in p.stdout:
        line = line.rstrip()

        # "/path/to/standalone/filename:"
        if line == filename + ':':
            if obj:
                objects.append(obj)
            obj = {'member': None,
                   'flags': set(),
                   'symbols': set()}
            continue

        # "/path/to/archive/filename[member]:"
        if line.startswith(filename + '[') and line.endswith(']:'):
            # /usr/lib/libc.a[shr.o]:
            if obj:
                objects.append(obj)
            obj = {'member': line[len(filename)+1:-2],
                   'flags': set(),
                   'symbols': set()}
            continue

        if not obj:
            continue

        # "Flags=( ... )"
        if line.startswith('Flags=( ') and line.endswith(' )'):
            flags = set(line[8:-2].split())
            if not 'DYNLOAD' in flags:
                obj = None
                continue
            obj['flags'] = flags
            continue

        # "[0]   0x00000000 undef ImpExp UA EXTref /unix    _environ"
        # "[748] 0x0006b0f0 .data    EXP DS SECdef [noIMid] malloc"
        if line.startswith('[') and not line.startswith('[Index]'):
            symparts = line.split()
            if symparts[3] in ('EXP', 'ImpExp'):
                obj['symbols'].add(symparts[7])
                continue

    if obj:
        objects.append(obj)

    p.stdout.close()
    p.wait()

    return objects


def split_filename_member(libname):
    """split_filename_member(libname) -> tuple(filename, member)

    When libname defines valid "filename(member)", these are returned.
    Otherways, tuple(libname, None) is returned.
    """

    baseparts = libname.split(")")
    if len(baseparts) == 2 and len(baseparts[1]) == 0: # ends with ')'
        baseparts = baseparts[0].split("(")
        if len(baseparts) == 2 and len(baseparts[0]) > 0 and len(baseparts[1]) > 0:
            # really got "filename(member)"
            return (baseparts[0], baseparts[1])
    return (libname, None)


def get_libpath():
    """get_libpath() -> list of paths to search for runtime libraries

    The AIX loadquery procedure "Returns the library path that was used
    at process exec time."
    For the current library path the LIBPATH environment variable - or
    LD_LIBRARY_PATH if LIBPATH is unset, is prepended.
    """

    envlibpath = os.environ.get('LIBPATH', None)
    if not envlibpath:
        envlibpath = os.environ.get('LD_LIBRARY_PATH', '')

    libpath = envlibpath.split(':')

    from _ctypes_aix import loadquery, L_GETLIBPATH

    syslibpath = loadquery(L_GETLIBPATH)
    if syslibpath:
        libpath = libpath + syslibpath

    return libpath


def get_searchpath_libname(name):
    """get_searchpath_libname(name) -> tuple([searchpath], libname)

    When name contains some path part, this is returned as searchpath.
    Otherways, get_libpath() is returned as searchpath.
    """

    path, libname = os.path.split(name)
    if path:
        return ([path], libname)
    return (get_libpath(), libname)


def guess_searchnames(basename, membername):
    """guess_searchnames(basename, membername) ->
        list of tuples (filename, set of valid membernames)

    Does some wild guessing based on the contents of BASENAME and MEMBERNAME.

    Each tuple returned is the filename and a set of valid membernames,
    where the first matching membername should be preferred, and the
    None membername should match any member without the LOADONLY flag.
    """

    searchnames = []

    # With an extension, we got a complete filename, so we should
    # not search for filenames like the linker does with "-lNAME".

    if basename.endswith('.so'): # explicit filename
        # try '.so'
        searchnames.append((basename, set([membername])))
        # try '.a'
        searchnames.append((basename[:-3] + '.a', set([membername])))

    elif '.so.' in basename: # explicit filename with version
        found = basename.rfind('.so.')
        name_a = basename[:found] + '.a'
        name_av = name_a + basename[found+3:]
        # try versioned '.so'
        searchnames.append((basename, set([membername])))
        if membername:
            # try versioned '.a'
            searchnames.append((name_av, set([membername])))
        else:
            # try versioned '.a' with basename as member
            # try versioned '.a' with active member
            searchnames.append((name_av, set([basename, None])))
            ## the traditional libtool variants ##
            # try unversioned '.a' with basename as member
            searchnames.append((name_a, set([basename])))

    elif basename.endswith('.a'): # explicit extension
        # try '.a'
        searchnames.append((basename, set([membername])))
        # try '.so'
        searchnames.append((basename[:-2] + '.so', set([membername])))

    elif '.a.' in basename: # explicit extension + version
        found = basename.rfind('.a.')
        name_so = basename[:found] + '.so'
        name_sov = name_so + basename[found+2:]
        # try versioned '.a'
        searchnames.append((basename, set(membername)))
        # try versioned '.so'
        searchnames.append((name_sov, set(membername)))

    else: # linker-style (-lNAME)
        # There is no extension or version, so the linker algorithm applies.
        # try 'libNAME.so'
        searchnames.append(('lib'+basename+'.so', set([membername])))
        if basename.startswith('lib'):
            # try 'NAME.so'
            searchnames.append((basename+'.so', set([membername])))
        # try 'libNAME.a'
        searchnames.append(('lib'+basename+'.a', set([membername])))
        if basename.startswith('lib'):
            # try 'NAME.a'
            searchnames.append((basename+'.a', set([membername])))
        # try 'NAME' as fallback
        searchnames.append((basename, set([membername])))

    return searchnames


def search_file(searchpath, searchnames):
    """search_file(searchpath, searchnames) ->
        (filename, matched searchnames entry)

    Along searchpath, try to stat each path/searchname[0] as filename.
    The first existing filename is returned, along the searchnames
    entry which provided the existing filename.
    """
    for path in searchpath:
        for searchentry in searchnames:
            filename = os.path.join(path, searchentry[0])
            if os.path.exists(filename):
                # The first file found will be used,
                # even if would not fit anything.
                return filename, searchentry
    return None, None


def find_library(name):
    """AIX platform implementation of find_library()
    This routine is called by ctype.util.find_library().
    This routine should not be called directly!
    
    AIX loader behavior:
    Find a library looking first for an archive (.a) with a suitable member
    For the loader the member name is not relevant. The first archive object
    shared or static that resolves an unknown symbol is used by "ld"

    To mimic AIX rtld behavior this routines looks first for an archive
    libFOO.a and then examines the contents of the archive for shareable
    members that also match either libFOO.so, libFOO.so.X.Y.Z (so-called
    versioned library names) and finally AIX legacy names
    usually shr.o (32-bit members) and shr_64.o (64-bit members)
    
    When no archive(member) is found, look for a libFOO.so file
    When versioning is required - it must be provided via hard
    or soft-link from libFOO.so to the correct version.
    RTLD aka dlopen() does not do versioning.
    """

    def match_membernames(objects, validmembers):
        matching = []
        for obj in objects:
            member = obj['member'] # None for standalone object
            if member in validmembers:
                matching.append(obj)
        if not matching and None in validmembers:
            return objects
        return matching

    def filter_inactive_members(objects):
        active = []
        for obj in objects:
            if 'LOADONLY' not in obj['flags']:
                active.append(obj)
        return active

    searchpath, libname = get_searchpath_libname(name)
    if not libname:
        return None

    basename, membername = split_filename_member(libname)
    if not basename:
        return None

    searchnames = guess_searchnames(basename, membername)
    if not searchnames:
        return None

    filename, applied_searchname = search_file(searchpath, searchnames)
    if not filename:
        return None
    (basename, validmembers) = applied_searchname

    remaining = dump_objectfile(filename)
    if not remaining:
        return None

    # Apply various filters on all remaining objects to find useable ones,
    # identified along their member names and "flags" eventually.
    # For standalone object file, {"member"} is None, which
    # will match the None value in validmembers just fine.

    # Really use matching member names only.
    remaining = match_membernames(remaining, validmembers)

    if len(remaining) > 1:
        # If we have more than one matching member, reduce to active ones.
        remaining = filter_inactive_members(remaining)

    # We may fail to spot anything useful.
    if not remaining:
        return None

    # If we still have more than one member remaining, we may
    # want to return each for _ctypes_aix.dlopen() to load.
    members = ''
    for obj in remaining:
        if obj['member']:
            if members:
                members = members + ','
            members = members + obj['member']
            break # for now return first one only

    if members:
        return filename + '(' + members + ')'
    return filename
