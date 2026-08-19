#ifndef _WIMLIB_TYPES_H
#define _WIMLIB_TYPES_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

#if defined(_WIN32) && !defined(__MINGW32__)
/*
 * Windows, with a toolchain that targets the MSVC runtime rather than MinGW.
 *
 * MinGW ships a small POSIX compatibility layer -- <unistd.h>, ssize_t from
 * <sys/types.h>, the STD*_FILENO constants -- which the Windows SDK does not
 * have, and which wimlib's platform-independent code uses in a handful of
 * places.  Supplied here so those places need no #ifdef of their own.  The
 * functions themselves do exist in the MSVC runtime: <io.h> declares close(),
 * read(), write() and unlink() under exactly those names.
 */
#  include <io.h>
#  include <time.h>
#  include <errno.h>
#  include <sys/types.h>
#  include <basetsd.h>
typedef SSIZE_T ssize_t;

/*
 * THE MSVC RUNTIME'S off_t IS A 32-BIT long, and _FILE_OFFSET_BITS does nothing
 * there -- it is a glibc/MinGW knob.  wimlib uses off_t for offsets within a WIM,
 * which routinely pass 2 GiB (the install.wim inside a Windows ISO is several GiB
 * on its own), so leaving it 32-bit would not fail to build; it would silently
 * truncate.  Overriding it is safe because <sys/types.h> above has already been
 * read and is include-guarded, and nothing else in the runtime declares the name.
 *
 * lseek() must follow it to the 64-bit variant for the same reason.  Note that
 * read(), write(), pread() and pwrite() do NOT need this: win32_replacements.c
 * substitutes all four on Windows.
 */
#  undef off_t
#  define off_t		__int64
#  define lseek		_lseeki64

/* ftruncate() in terms of _chsize_s(), which reports an errno as its return
 * value rather than through errno itself.  */
static inline int
ftruncate(int fd, off_t length)
{
	int err = _chsize_s(fd, length);

	if (err) {
		errno = err;
		return -1;
	}
	return 0;
}
#  define STDIN_FILENO	0
#  define STDOUT_FILENO	1
#  define STDERR_FILENO	2
/* access() modes; MinGW puts these in <unistd.h>.  Windows has no execute
 * permission bit, so X_OK is spelled the same as R_OK, which is what MinGW
 * does too.  */
#  define F_OK		0
#  define W_OK		2
#  define R_OK		4
#  define X_OK		4
/* POSIX gmtime_r in terms of the MSVC runtime's gmtime_s.  Note the argument
 * order is reversed between the two, and gmtime_s returns an errno rather than
 * a pointer.  */
static inline struct tm *
gmtime_r(const time_t *timep, struct tm *result)
{
	return gmtime_s(result, timep) ? NULL : result;
}
#endif

#include "wimlib_tchar.h"
#include "wimlib/compiler.h"

#ifndef _NTFS_TYPES_H
/* Unsigned integer types of exact size in bits */
typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* Signed integer types of exact size in bits */
typedef int8_t  s8;
typedef int16_t s16;
typedef int32_t s32;
typedef int64_t s64;

/* Unsigned little endian types of exact size */
typedef uint16_t _bitwise_attr le16;
typedef uint32_t _bitwise_attr le32;
typedef uint64_t _bitwise_attr le64;

/* Unsigned big endian types of exact size */
typedef uint16_t _bitwise_attr be16;
typedef uint32_t _bitwise_attr be32;
typedef uint64_t _bitwise_attr be64;
#endif

/* A pointer to 'utf16lechar' indicates a UTF-16LE encoded string */
typedef le16 utf16lechar;

#ifndef WIMLIB_WIMSTRUCT_DECLARED
typedef struct WIMStruct WIMStruct;
#  define WIMLIB_WIMSTRUCT_DECLARED
#endif

/*
 * Type of a machine word.  'unsigned long' would be logical, but that is only
 * 32 bits on x86_64 Windows.  The same applies to 'uint_fast32_t'.  So the best
 * we can do without a bunch of #ifdefs appears to be 'size_t'.
 */
typedef size_t machine_word_t;

#define WORDBYTES	sizeof(machine_word_t)
#define WORDBITS	(8 * WORDBYTES)

#endif /* _WIMLIB_TYPES_H */
