#ifndef _WIMLIB_FILE_IO_H
#define _WIMLIB_FILE_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/types.h>

struct wimlib_file_reader;

/* Wrapper around a file descriptor that keeps track of offset (including in
 * pipes, which don't support lseek()) and a cached flag that tells whether the
 * file descriptor is a pipe or not.  When 'reader' is set there is no file
 * descriptor at all: every read goes through the reader instead (see
 * wimlib_open_wim_from_reader()).  */
struct filedes {
	int fd;
	unsigned int is_pipe : 1;
	off_t offset;
	const struct wimlib_file_reader *reader;
	void *reader_ctx;
};

int
full_read(struct filedes *fd, void *buf, size_t n);

int
full_pread(struct filedes *fd, void *buf, size_t nbyte, off_t offset);

int
full_write(struct filedes *fd, const void *buf, size_t n);

int
full_pwrite(struct filedes *fd, const void *buf, size_t count, off_t offset);

#ifndef _WIN32
#  define O_BINARY 0
#endif

off_t
filedes_seek(struct filedes *fd, off_t offset);

bool
filedes_is_seekable(struct filedes *fd);

static inline void filedes_init(struct filedes *fd, int raw_fd)
{
	fd->fd = raw_fd;
	fd->offset = 0;
	fd->is_pipe = 0;
	fd->reader = NULL;
	fd->reader_ctx = NULL;
}

static inline void filedes_invalidate(struct filedes *fd)
{
	fd->fd = -1;
	fd->reader = NULL;
}

/* A reader is the caller's to free (see wimlib_open_wim_from_reader()), so
 * closing one only lets go of it.  */
#define filedes_close(f) ((f)->reader ? ((f)->reader = NULL, 0) : close((f)->fd))

static inline bool
filedes_valid(const struct filedes *fd)
{
	return fd->fd != -1 || fd->reader != NULL;
}

#endif /* _WIMLIB_FILE_IO_H */
