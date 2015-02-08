#ifndef __HAKIT_TYPES_H__
#define __HAKIT_TYPES_H__

#define ARRAY_SIZE(x) (sizeof(x)/sizeof(x[0]))
#define CEIL_DIV(x, y) (((x)+(y)-1) / (y))

#ifndef MIN
#define MIN(x,y) (((x)<(y))?(x):(y))
#endif

#ifndef MAX
#define MAX(x,y) (((x)>(y))?(x):(y))
#endif

#undef offsetof
#ifdef __compiler_offsetof
#define offsetof(TYPE,MEMBER) __compiler_offsetof(TYPE,MEMBER)
#else
#define offsetof(TYPE, MEMBER) ((unsigned long) &((TYPE *)0)->MEMBER)
#endif

/**
 * container_of - cast a member of a structure out to the containing structure
 * @ptr:	the pointer to the member.
 * @type:	the type of the container struct this is embedded in.
 * @member:	the name of the member within the struct.
 *
 */
#define container_of(ptr, type, member) ({			\
	const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
	(type *)( (char *)__mptr - offsetof(type,member) );})

/* C preprocessor stringification macros */
#define xstr(s) str(s)
#define str(s) #s

#endif  /* __HAKIT_TYPE_H__ */
