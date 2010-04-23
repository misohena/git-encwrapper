#ifndef CACHE_H_INCLUDED_
#define CACHE_H_INCLUDED_


#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <fnmatch.h>
#include <memory.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "strbuf.h"

// git-compat-util.h
#define FLEX_ARRAY /* empty */
#define PATH_MAX 4096
extern void release_pack_memory(size_t, int);
extern void *xmalloc(size_t size);
extern void *xrealloc(void *ptr, size_t size);
extern void *xmemdupz(const void *data, size_t len);
extern void *xcalloc(size_t nmemb, size_t size);

#define strchrnul gitstrchrnul
static inline char *gitstrchrnul(const char *s, int c)
{
	while (*s && *s != c)
		s++;
	return (char *)s;
}


// cache.h
#define ATTRIBUTE_MACRO_PREFIX "[attr]"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
static inline const char *git_path(const char *fmt) {return fmt;}
static inline int is_bare_repository(void) {return 0;}
static inline void die(const char *err, ...) {exit(-1);}
#define alloc_nr(x) (((x)+16)*3/2)
/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x' or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			x = xrealloc((x), alloc * sizeof(*(x))); \
		} \
	} while(0)



#endif
