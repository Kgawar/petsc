#ifdef PETSC_RCS_HEADER
"$Id: petscconf.h,v 1.13 1999/02/08 22:22:33 bsmith Exp bsmith $"
"Defines the configuration for this machine"
#endif

#if !defined(INCLUDED_PETSCCONF_H)
#define INCLUDED_PETSCCONF_H

#define PARCH_rs6000

#define HAVE_STROPTS_H 
#define HAVE_SEARCH_H 
#define HAVE_PWD_H 
#define HAVE_STDLIB_H
#define HAVE_STRING_H 
#define HAVE_STRINGS_H 
#define HAVE_MALLOC_H 
#define _POSIX_SOURCE
#define HAVE_DRAND48  
#define HAVE_GETDOMAINNAME  
#if !defined(_XOPEN_SOURCE)
#define _XOPEN_SOURCE 
#endif
#define HAVE_X11
#define HAVE_UNISTD_H 
#define HAVE_SYS_TIME_H 
#define HAVE_UNAME 
#if !defined(_XOPEN_SOURCE_EXTENDED)
#define _XOPEN_SOURCE_EXTENDED 1
#endif
#define _ALL_SOURCE
#define HAVE_DOUBLE_ALIGN_MALLOC

#define HAVE_XLF90

#define PREFER_BZERO

#define HAVE_READLINK
#define HAVE_MEMMOVE

#define USE_DBX_DEBUGGER
#define HAVE_SYS_RESOURCE_H
#define SIZEOF_VOIDP 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_INT 4

#define WORDS_BIGENDIAN 1

#endif
