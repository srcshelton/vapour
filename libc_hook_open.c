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

#define DEBUG 0

#if DEBUG
# define debug_fprintf(stream, fmt, ...) \
	do { if (DEBUG) fprintf( stream, "%s:%d:%s(): " fmt, __FILE__, \
		__LINE__, __func__, __VA_ARGS__); } while (0) // C99
# define TRACE(x) do { if (DEBUG) dbg_printf x; } while (0) // C89
#else
# define debug_fprintf(stream, fmt, ...)
# define TRACE(x)
#endif


/* Hooking the actual Mach libc implementation of open()/close() et al. (which
 * may in fact be implemented by open_nocancel, close_nocancel, etc.) also
 * prevents *anything* which calls open() (including (f)printf(), fopen(),
 * etc.) from running, as the function-call gets stuck in a loop.
 */
//#define HOOK_IMPLEMENTATION 1
#undef HOOK_IMPLEMENTATION

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

#define DYLD_INTERPOSE(_replacment,_replacee) \
	__attribute__((used)) static struct{ const void* replacment; const void* replacee; } _interpose_##_replacee \
	__attribute__ ((section ("__DATA,__interpose"))) = { (const void*)(unsigned long)&_replacment, (const void*)(unsigned long)&_replacee }; 

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


/* Usage:
 *
 * TRACE(("message %d\n", var));
 *
 * ... the double-parentheses are crucial — and are why you have the funny
 * notation in the macro expansion.  The compiler always checks the code for
 * syntactic validity (which is good) but the optimizer only invokes the
 * print function if the DEBUG macro evaluates to non-zero.
 */
void dbg_printf(const char *fmt, ...) {
	va_list args;
	va_start( args, fmt);
		vfprintf( stderr, fmt, args );
	va_end( args );
}

int capitalisepath(const char *string, char **result) {
	unsigned char lastchar = 0;
	unsigned long len;

	debug_fprintf( stderr, "capitalisepath starting with '%s'\n", string );

	/* strlen(string) should be less than PATH_MAX, but this isn't enforced
	 * anywhere...
	 */
	errno = 0;
	if(( *result = (char *)malloc( strlen( string ) + 1 ) )) {
		/* We can assume that the path won't be longer than 4 thousand million
		 * characters... ?
		 */
		unsigned int max;
		for( max = strlen( string ) - 1 ; max > 0 ; --max ) {
			unsigned char c = string[ max ];

			debug_fprintf( stderr, "capitalisepath read char %c\n", c );

			if( c == '/' )
				break;
		}

		debug_fprintf( stderr, "capitalisepath last separator is at %d\n", max );

		for( unsigned int n = 0 ; n < max ; n++ ) {
			unsigned char c = string[ n ];

			debug_fprintf( stderr, "capitalisepath read char %c\n", c );

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

				debug_fprintf( stderr, "capitalisepath using char %c\n", (*result)[ n ] );
			}

			debug_fprintf( stderr, "capitalisepath lastchar is %c\n", lastchar );
		}
		for( ; max < strlen( string ) ; max++ ) {
			unsigned char c = string[ max ];
			(*result)[ max ] = c;

			debug_fprintf( stderr, "capitalisepath appending char %c\n", c );
		}
		(*result)[ strlen( string ) ] = '\0';

		debug_fprintf( stderr, "capitalisepath string is %s\n", *result );

		len = strlen( *result );

		debug_fprintf( stderr, "capitalisepath returning %lu\n", len );

		if( len > INT_MAX ) {
			return 0;
		} else {
			return (int)len;
		}
	} else {
		debug_fprintf( stderr, "capitalisepath malloc failed with error %d: %s\n", errno, strerror( errno ) );

		if( 0 == errno )
			errno = ENOMEM;
	}

	return -1;
}  // capitalisepath

int hooked_open(const char *pathname, int flags, ...) {
	mode_t mode;
	int m = 0;
	int o;
	va_list ap;

	va_start( ap, flags );
			switch( sizeof( mode_t ) ) {
				case 1:  mode = va_arg(ap, int); break;
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

	if( 1 == m ) {
		fprintf( stderr, "->  Intercepted three-arg open('%s', %d, %ju)", pathname, flags, CONVERT_TO_MAX(mode_t, mode) );
	} else {
		fprintf( stderr, "->  Intercepted two-arg open('%s', %d)", pathname, flags );
	}

	if( flags & O_CREAT ) {
#if DEBUG
		fprintf( stderr, ".\n--> File '%s' will be created\n", pathname );
#endif
	} else if( flags & ( O_CREAT | O_EXCL ) ) {
#if DEBUG
		fprintf( stderr, ".\n--> File '%s' will be created if it doesn't exist\n", pathname );
#endif
	} else {
		if( ( o = open_implementation( pathname, O_NONBLOCK | O_EVTONLY ) ) >= 0 ) {
			close( o );
#if DEBUG
			fprintf( stderr, ".\n--> File '%s' exists (%d)\n", pathname, o );
			fprintf( stderr, "--> open()" );
#endif
		} else {
			char *newpath;

			fprintf( stderr, ".\n--> File '%s' does not exist\n", pathname );

			if( capitalisepath( pathname, &newpath ) >= 0 ) {
				// debug_fprintf( stderr, "--> %s\n", "Got back" ); // must have >=3 args
				debug_fprintf( stderr, "--> Corrected path is '%s'\n", newpath );
				if( ( o = open_implementation( newpath, O_NONBLOCK | O_EVTONLY ) ) >= 0 ) {
					close( o );
					fprintf( stderr, "--> Corrected file '%s' exists\n", newpath );
					pathname = newpath;
				} else {
					fprintf( stderr, "--> Corrected file '%s' does not exist\n", newpath );
				}
			}
			fprintf( stderr, "--> open()" );
		}
	}

	if( 1 == m ) {
		o = open_implementation( pathname, flags, mode );
	} else {
		o = open_implementation( pathname, flags );
	}
	if( o >= 0 ) {
		fprintf( stderr, ": %d\n", o );
	} else {
		fprintf( stderr, ": %s\n", strerror( errno ) );
	}

	return o;
} // hooked_open

/* Hooking close() is only useful when we're able to produce output in order to
 * indicate when open()ed file descriptors are finished with...
 */
#ifndef HOOK_IMPLEMENTATION
int hooked_close(int fd) {
	int c = 0;

	fprintf( stderr, "->  Intercepted close(%d)", fd );
	c = close_implementation( fd );
	if( c >= 0 ) {
		fprintf( stderr, ": %d\n", c );
	} else {
		fprintf( stderr, ": %d - %s\n", c, strerror( errno ) );
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
