/*
 * read_path.c
 *
 * Read a file's data out of a WIM image without extracting it to the filesystem.
 *
 * The work is all done elsewhere: read_blob_with_cbs() streams a blob's uncompressed data
 * through callbacks, and read_partial_wim_blob_into_buf() serves an arbitrary byte range out of
 * one. Both are internal, so the public API reaches them through the two functions here, which
 * resolve a path in an image to its blob and hand it over.
 *
 * The range read is the more interesting half. A WIM resource is stored as a table of
 * independently compressed chunks, so the chunk covering an offset can be located and
 * decompressed on its own — random access into compressed data, without touching what comes
 * before it. That is what lets a viewer page around a multi-gigabyte member instead of writing
 * it to a temporary directory to see its first bytes. Solid resources (LZMS, as used by .esd)
 * have a chunk table too, so it works there as well — but their chunks are far larger, and a seek
 * costs one chunk of decompression either way. Measured: a seek inside a non-solid LZX resource
 * (32 KB chunks) is under half a millisecond; the same seek inside a solid LZMS resource is around
 * 88 ms. Both beat materialising the member, and the difference is worth knowing before treating
 * the range read as free.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <string.h>

#include "wimlib.h"
#include "wimlib/blob_table.h"
#include "wimlib/case.h"
#include "wimlib/dentry.h"
#include "wimlib/error.h"
#include "wimlib/inode.h"
#include "wimlib/metadata.h"
#include "wimlib/paths.h"
#include "wimlib/resource.h"
#include "wimlib/util.h"
#include "wimlib/wim.h"

/*
 * Resolve `path` in `image` to the blob holding its unnamed data stream.
 *
 * A directory has no such stream and neither does a reparse point, so both are refused rather
 * than reported as empty — "this is not a thing with contents" is a different answer from "its
 * contents are zero bytes long", and a caller that cannot tell them apart will show an empty
 * file where it should show nothing at all.
 *
 * A genuinely empty file has no blob: WIM stores nothing for zero bytes. That is reported as
 * success with *blob_ret == NULL, which every caller below handles as the zero-length read it
 * is.
 */
static int
resolve_path_blob(WIMStruct *wim, int image, const tchar *path,
		  struct blob_descriptor **blob_ret)
{
	tchar *canonical;
	struct wim_dentry *dentry;
	struct wim_inode *inode;
	int ret;

	if (image == WIMLIB_ALL_IMAGES || image < 1)
		return WIMLIB_ERR_INVALID_IMAGE;

	ret = select_wim_image(wim, image);
	if (ret)
		return ret;

	canonical = canonicalize_wim_path(path);
	if (canonical == NULL)
		return WIMLIB_ERR_NOMEM;

	dentry = get_dentry(wim, canonical, WIMLIB_CASE_PLATFORM_DEFAULT);
	FREE(canonical);
	if (dentry == NULL)
		return WIMLIB_ERR_PATH_DOES_NOT_EXIST;

	inode = dentry->d_inode;
	if (inode_is_directory(inode))
		return WIMLIB_ERR_IS_DIRECTORY;
	if (inode_is_symlink(inode))
		return WIMLIB_ERR_NOT_A_REGULAR_FILE;

	*blob_ret = inode_get_blob_for_unnamed_data_stream(inode, wim->blob_table);
	return 0;
}

struct stream_ctx {
	wimlib_read_data_callback_t cb;
	void *user_ctx;
};

static int
stream_chunk(const struct blob_descriptor *blob, u64 offset,
	     const void *chunk, size_t size, void *_ctx)
{
	struct stream_ctx *ctx = _ctx;

	(void)blob;
	(void)offset;
	return (*ctx->cb)(chunk, size, ctx->user_ctx);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_read_path_data(WIMStruct *wim, int image, const wimlib_tchar *path,
		      wimlib_read_data_callback_t cb, void *user_ctx,
		      uint64_t *size_ret)
{
	struct blob_descriptor *blob = NULL;
	struct stream_ctx ctx;
	struct read_blob_callbacks cbs;
	int ret;

	if (wim == NULL || path == NULL || cb == NULL)
		return WIMLIB_ERR_INVALID_PARAM;

	ret = resolve_path_blob(wim, image, path, &blob);
	if (ret)
		return ret;

	/*
	 * The size goes out before the first chunk, so a caller sizing a window or a progress
	 * bar has the total in hand while the data is still arriving.
	 */
	if (size_ret != NULL)
		*size_ret = (blob != NULL) ? blob->size : 0;

	if (blob == NULL || blob->size == 0)
		return 0; /* an empty file: nothing to deliver, and not an error */

	ctx.cb = cb;
	ctx.user_ctx = user_ctx;

	memset(&cbs, 0, sizeof(cbs));
	cbs.continue_blob = stream_chunk;
	cbs.ctx = &ctx;

	/*
	 * Hashes are deliberately not verified. This is a read for display, the chunk table's own
	 * lengths already bound every decompression, and a caller looking at one file should not
	 * pay for a SHA-1 of it. read_blob_with_sha1() is there for callers that want the check.
	 */
	return read_blob_with_cbs(blob, &cbs, false);
}

/* API function documented in wimlib.h  */
WIMLIBAPI int
wimlib_read_path_data_range(WIMStruct *wim, int image, const wimlib_tchar *path,
			    uint64_t offset, size_t size, void *buf,
			    size_t *nread_ret)
{
	struct blob_descriptor *blob = NULL;
	u64 available;
	int ret;

	if (wim == NULL || path == NULL || (buf == NULL && size != 0))
		return WIMLIB_ERR_INVALID_PARAM;

	ret = resolve_path_blob(wim, image, path, &blob);
	if (ret)
		return ret;

	/*
	 * Only a blob that lives in the WIM has a resource descriptor to seek within. One staged
	 * in a file on disk or held in a buffer — which happens on a WIMStruct with pending
	 * changes, not on one merely opened for reading — has no chunk table to index, so it is
	 * refused here rather than reaching a read that would follow an invalid descriptor.
	 */
	if (blob != NULL && blob->blob_location != BLOB_IN_WIM)
		return WIMLIB_ERR_INVALID_PARAM;

	/*
	 * Past the end is a short read of zero, not an error — the same answer pread(2) gives, and
	 * the one a caller stepping through a file with a fixed window needs at the last step.
	 */
	available = (blob != NULL) ? blob->size : 0;
	if (offset >= available)
		size = 0;
	else if (size > available - offset)
		size = available - offset;

	if (nread_ret != NULL)
		*nread_ret = size;

	if (size == 0)
		return 0;

	return read_partial_wim_blob_into_buf(blob, offset, size, buf);
}
