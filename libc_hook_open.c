#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <ctype.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <sys/cdefs.h>
#include <sys/errno.h>

/* Hooking the actual Mach libc implementation of open()/close() et al. (which
 * may in fact be implemented by open_nocancel, close_nocancel, etc.) also
 * prevents *anything* which calls open() (including (f)printf(), fopen(),
 * etc.) from running, as otherwise the function-call gets stuck in a loop.
 */
//#define HOOK_IMPLEMENTATION 1
#undef HOOK_IMPLEMENTATION

//#define DEBUG 1
#undef DEBUG


#if DEBUG
# define debug_fprintf(stream, fmt, ...) \
	do { if (DEBUG) fprintf( stream, "%s:%d:%s(): " fmt, __FILE__, \
		__LINE__, __func__, __VA_ARGS__); } while (0) // C99
# define verbose_fprintf(stream, fmt, ...) \
	do { if (DEBUG) fprintf( stream, fmt, __VA_ARGS__); } while (0) // C99
#else
# define debug_fprintf(stream, fmt, ...)
# define verbose_fprintf(stream, fmt, ...)
#endif


#ifdef HOOK_IMPLEMENTATION
# define fprintf(s, f, ...)
#endif

#if !__DARWIN_NON_CANCELABLE
# if __DARWIN_UNIX03 && !__DARWIN_ONLY_UNIX_CONFORMANCE
#  define open_interface open$UNIX2003
#  define open_implementation open$NOCANCEL$UNIX2003
#  ifndef HOOK_IMPLEMENTATION
#   define close_interface close$UNIX2003
#   define close_implementation close$NOCANCEL$UNIX2003
#  endif
# elif !__DARWIN_UNIX03 && !__DARWIN_ONLY_UNIX_CONFORMANCE
#  define open_interface open
#  define open_implementation open$NOCANCEL$UNIX2003
#  ifndef HOOK_IMPLEMENTATION
#    define close_interface close
#    define close_implementation close$NOCANCEL$UNIX2003
#  endif
# else // __DARWIN_ONLY_UNIX_CONFORMANCE
#  define open_interface open
#  define open_implementation open$NOCANCEL
#  ifndef HOOK_IMPLEMENTATION
#    define close_interface close
#    define close_implementation close$NOCANCEL
#  endif
# endif
#else // __DARWIN_NON_CANCELABLE
# define open_interface open
# define open_implementation open
#  ifndef HOOK_IMPLEMENTATION
#   define close_interface close
#   define close_implementation close
#  endif
#endif // !__DARWIN_NON_CANCELABLE

#define DYLD_INTERPOSE(_replacement,_replacee) \
	__attribute__((used)) static struct{ const void* replacement; const void* replacee; } _interpose_##_replacee \
	__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacement, (const void*)(unsigned long)&_replacee };

#define IS_SIGNED(type) ((type)-1 < (type)0)
#define DECIMAL_FORMAT(type) (IS_SIGNED(type) ? "%jd" : "%ju")
#define CONVERT_TO_MAX(type, value) \
	(IS_SIGNED(type) ? (intmax_t)(value) : (uintmax_t)(value))


void dbg_printf(const char *, ...);
int capitalisepath(const char *string, char **result);
#if !__DARWIN_NON_CANCELABLE
extern int open_implementation(const char *, int, ...);
# ifndef HOOK_IMPLEMENTATION
extern int close_implementation(int);
# endif
#endif
int hooked_open(const char *, int, ...);
#ifndef HOOK_IMPLEMENTATION
int hooked_close(int);
#endif


int capitalisepath(const char *string, char **result) {
	unsigned char lastchar = 0;
	unsigned long len;

	//debug_fprintf( stderr, "capitalisepath starting with '%s'\n", string );

	/* strlen(string) should be less than PATH_MAX, but this isn't enforced
	 * anywhere...
	 */
	errno = 0;

	if( !( *result = (char *)malloc( strlen( string ) + 1 ) ) ) {
		debug_fprintf( stderr, "capitalisepath malloc failed with error %d: %s\n", errno, strerror( errno ) );

		return -1;
	}

	/* We can assume that the path won't be longer than 4 thousand million
	 * characters... ?
	 */
	unsigned int max;
	for( max = strlen( string ) - 1 ; max > 0 ; --max ) {
		unsigned char c = string[ max ];

		//debug_fprintf( stderr, "capitalisepath read char %c\n", c );

		if( c == '/' )
			break;
	}

	//debug_fprintf( stderr, "capitalisepath last separator is at %d\n", max );

	for( unsigned int n = 0 ; n < max ; n++ ) {
		unsigned char c = string[ n ];

		//debug_fprintf( stderr, "capitalisepath read char %c\n", c );

		if( 0 == c )
			break;
		if( 0 == n ) {
			lastchar = c;
			if( lastchar != '/' )
				(*result)[ n ] = toupper( c );
			else
				(*result)[ n ] = c;
		} else {
			if( lastchar == '/' || lastchar == ' ' )
				(*result)[ n ] = toupper( c );
			else
				(*result)[ n ] = c;
			lastchar = c;

			//debug_fprintf( stderr, "capitalisepath using char %c\n", (*result)[ n ] );
		}

		//debug_fprintf( stderr, "capitalisepath lastchar is %c\n", lastchar );
	}
	for( ; max < strlen( string ) ; max++ ) {
		unsigned char c = string[ max ];
		(*result)[ max ] = c;

		//debug_fprintf( stderr, "capitalisepath appending char %c\n", c );
	}
	(*result)[ strlen( string ) ] = '\0';

	//debug_fprintf( stderr, "capitalisepath string is %s\n", *result );

	len = strlen( *result );

	//debug_fprintf( stderr, "capitalisepath returning %lu\n", len );

	if( len > INT_MAX ) {
		return 0;
	} else {
		return (int)len;
	}

	/*
	 * Unreachable
	 */
}  // capitalisepath

int hooked_open(const char *path, int flags, ...) {
	char *pathname;
	unsigned long size;

	unsigned char m = 0;
	int o;

	mode_t mode;
	va_list ap;

	va_start( ap, flags );
			switch( sizeof( mode_t ) ) {
				case 1: mode = va_arg(ap, int); break;
				case 2: mode = va_arg(ap, int); break;
				case 4: mode = va_arg(ap, long); break;
				case 8: mode = va_arg(ap, long long); break;
				default:
					/* fatal error */
					errno = EOVERFLOW;
					return -1;
			}
			m = 1;
	va_end( ap );

	size = strlen( path ) + 1;
	if( !( pathname = malloc( size ) ) )
		return -1;
	if( strlcpy( pathname, path, size ) != size - 1 ) {
		errno = EOVERFLOW;
		return -1;
	}

	if( 1 == m ) {
		verbose_fprintf( stderr, "->  Intercepted three-arg open('%s', %d, %ju)", pathname, flags, CONVERT_TO_MAX(mode_t, mode) );
	} else {
		verbose_fprintf( stderr, "->  Intercepted two-arg open('%s', %d)", pathname, flags );
	}

	if( flags & O_CREAT ) {
		verbose_fprintf( stderr, "\n--> File '%s' will be created", pathname );
		verbose_fprintf( stderr, "\n--> %s", "open()" ); // must have >=3 args
	} else if( flags & ( O_CREAT | O_EXCL ) ) {
		verbose_fprintf( stderr, "\n--> File '%s' will be created if it doesn't exist", pathname );
		verbose_fprintf( stderr, "\n--> %s", "open()" ); // must have >=3 args
	} else {
		if( ( o = open_implementation( pathname, O_NONBLOCK | O_EVTONLY ) ) >= 0 ) {
			close( o );
			verbose_fprintf( stderr, "\n--> File '%s' exists (%d)", pathname, o );
			verbose_fprintf( stderr, "\n--> %s", "open()" ); // must have >=3 args
		} else {
			char *newpath;

			verbose_fprintf( stderr, "\n--> File '%s' does not exist\n", pathname );

			if( capitalisepath( pathname, &newpath ) >= 0 ) {
				verbose_fprintf( stderr, "--> Corrected path is '%s'\n", newpath );
				if( ( o = open_implementation( newpath, O_NONBLOCK | O_EVTONLY ) ) >= 0 ) {
					close( o );
					verbose_fprintf( stderr, "--> Corrected file '%s' exists\n", newpath );
					strlcpy( pathname, newpath, strlen( pathname ) + 1 );
				} else {
					verbose_fprintf( stderr, "--> Corrected file '%s' does not exist\n", newpath );
				}

				free( newpath );
			}
			verbose_fprintf( stderr, "--> %s", "open()" );
		}
	}

	if( 1 == m ) {
		o = open_implementation( pathname, flags, mode );
	} else {
		o = open_implementation( pathname, flags );
	}

	free( pathname );

	if( o >= 0 ) {
		verbose_fprintf( stderr, ": %d\n", o );
	} else {
		verbose_fprintf( stderr, ": %s\n", strerror( errno ) );
	}

	return o;
} // hooked_open

/* Hooking close() is only useful when we're able to produce output in order to
 * indicate when open()ed file descriptors are finished with...
 */
#ifndef HOOK_IMPLEMENTATION
int hooked_close(int fd) {
	int c = 0;

	verbose_fprintf( stderr, "->  Intercepted close(%d)", fd );
	c = close_implementation( fd );
	if( c >= 0 ) {
		verbose_fprintf( stderr, ": %d\n", c );
	} else {
		verbose_fprintf( stderr, ": %d - %s\n", c, strerror( errno ) );
	}

	return c;
} // hooked_close
#endif

/* Finally, specify the functions to override: */
#if (defined(HOOK_IMPLEMENTATION) && !__DARWIN_NON_CANCELABLE)
DYLD_INTERPOSE( hooked_open, open_implementation);
#else
DYLD_INTERPOSE( hooked_open, open);
DYLD_INTERPOSE( hooked_close, close);
#endif

/* vi: set ts=4: */
