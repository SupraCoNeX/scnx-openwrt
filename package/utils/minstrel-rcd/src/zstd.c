#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <errno.h>
#include <sys/stat.h>

#include <zstd.h>

#include "rcd.h"

static ZSTD_CDict *_dict = NULL;
static ZSTD_CCtx *_ctx = NULL;

struct buffer {
	void *buf;
	size_t size;
	size_t pos;
};

static struct buffer _in, _out;

static struct uloop_timeout timeout;
static int _timeout_ms;


static inline void
reset_timeout(void)
{
	uloop_timeout_set(&timeout, _timeout_ms);
}

static int
__compress(void *data, size_t len, void *dst, size_t dstlen, size_t *complen)
{
	size_t clen = ZSTD_compress_usingCDict(_ctx, dst, dstlen, data, len, _dict);
	if (ZSTD_isError(clen))
		goto error;

	*complen = clen;
	return 0;

error:
	fprintf(stderr, "compression failed: %s\n", ZSTD_getErrorName(clen));
	*complen = 0;
	return -1;
}

int
zstd_compress(void *data, size_t len, void **buf, size_t *buflen)
{
	size_t clen, dstlen = ZSTD_compressBound(len);
	int error;
	void *dst = malloc(dstlen);
	if (!dst)
		goto error;

	error = __compress(data, len, dst, dstlen, &clen);
	if (error)
		goto free;

	*buf = dst;
	*buflen = clen;
	return 0;

free:
	free(dst);
error:
	*buf = NULL;
	*buflen = 0;
	return -1;
}

int
zstd_compress_into(void *dst, size_t dstlen, void *data, size_t len, size_t *complen)
{
	size_t clen = ZSTD_compressBound(len);

	if (clen > dstlen) {
		fprintf(stderr, "cannot reliably compress data of size %zu (compress bound %zu) into buffer of size %zu\n", len, clen, dstlen);
		return -1;
	}

	return __compress(data, len, dst, dstlen, complen);
}

int
zstd_fmt_compress(void **buf, size_t *buflen, const char *fmt, ...)
{
	char *str, *tmp;
	size_t slen = 1024, n;
	va_list va_args;
	int error;

	str = malloc(slen);
	if (!str)
		return -ENOMEM;

	do {
		va_start(va_args, fmt);
		n = vsnprintf(str, slen, fmt, va_args);
		va_end(va_args);

		if (n >= slen) {
			/* grow buffer exponentially */
			slen *= 2;
			tmp = realloc(str, slen);
			if (!tmp) {
				error = -ENOMEM;
				goto error;
			}

			str = tmp;
			continue;
		}
	} while (0);

	error = zstd_compress(str, strlen(str) + 1, buf, buflen);
	free(str);

	return error;

error:
	free(str);
	*buf = NULL;
	*buflen = 0;
	return error;
}

static ZSTD_CDict*
load_dict(const char *path, int complvl)
{
	struct stat st;
	size_t size, read_size;
	off_t fsize;
	void *buf;
	FILE *file;
	ZSTD_CDict *dict;

	if (stat(path, &st)) {
		perror(path);
		return NULL;
	}

	fsize = st.st_size;
	size = (size_t) fsize;

	if ((fsize < 0) || (fsize != (off_t) size)) {
		fprintf(stderr, "%s: filesize too large\n", path);
		return NULL;
	}

	buf = malloc(size);
	if (!buf) {
		perror("malloc");
		return NULL;
	}

	file = fopen(path, "rb");
	if (!file) {
		perror(path);
		return NULL;
	}

	read_size = fread(buf, 1, size, file);
	if (read_size != size) {
		fprintf(stderr, "fread: %s : %s\n", path, strerror(errno));
		return NULL;
	}

	fclose(file);

	dict = ZSTD_createCDict(buf, size, complvl);
	free(buf);

	return dict;
}

static void
zstd_compress_and_flush(void)
{
	size_t remaining;
	bool done;
	ZSTD_inBuffer zinbuf;
	ZSTD_outBuffer zoutbuf;

	if (_in.pos == 0)
		goto done;

	zinbuf.src = _in.buf;
	zinbuf.size = _in.pos;
	zinbuf.pos = 0;

	do {
		zoutbuf.dst = _out.buf;
		zoutbuf.size = _out.size;
		zoutbuf.pos = 0;

		remaining = ZSTD_compressStream2(_ctx, &zoutbuf, &zinbuf, ZSTD_e_end);
		if (ZSTD_isError(remaining)) {
			fprintf(stderr, "stream compression error: %s\n", ZSTD_getErrorName(remaining));
			_in.pos = 0;
			goto done;
		}

		rcd_zclient_write(zoutbuf.dst, zoutbuf.pos);

		done = remaining == 0;
	} while (!done);

	_in.pos = 0;

done:
	reset_timeout();
}

int
zstd_read_fmt(const char *fmt, ...)
{
	/* don't bother filling the input buffer if there are no connected clients */
	if (!rcd_has_clients(true)) {
		uloop_timeout_cancel(&timeout);
		return 0;
	}

	size_t read, remaining;
	va_list va_args;

	remaining = _in.size - _in.pos;

	va_start(va_args, fmt);
	read = vsnprintf(_in.buf + _in.pos, remaining, fmt, va_args);
	va_end(va_args);

	if (read < remaining)
		goto ok;

	zstd_compress_and_flush();

	va_start(va_args, fmt);
	read = vsnprintf(_in.buf, _in.size, fmt, va_args);
	va_end(va_args);

	if (read < _in.size)
		goto ok;

	fprintf(stderr, "discarding data of length %zu: cannot fit into buffer of size: %zu\n", read, _in.size);

	return -1;

ok:
	_in.pos += read;
	if (!timeout.pending)
		reset_timeout();

	return 0;
}

static inline void
timeout_flush(struct uloop_timeout *t)
{
	zstd_compress_and_flush();
}

int
zstd_init(const struct zstd_opts *o)
{
	static const char *EMSG_NODICT = "no dictionary provided";
	static const char *EMSG_LOADFAILED = "error loading dictionary";
	static const char *EMSG_NOCTX = "error creating zstd context";
	static const char *EMSG_BUFALLOC = "error allocating buffers";

	const char *errmsg = NULL;

	if (!o->dict) {
		errmsg = EMSG_NODICT;
		goto error;
	}

	_dict = load_dict(o->dict, o->comp_level);
	if (!_dict) {
		errmsg = EMSG_LOADFAILED;
		goto error;
	}

	_ctx = ZSTD_createCCtx();
	if (!_ctx) {
		errmsg = EMSG_NOCTX;
		goto free;
	}

	ZSTD_CCtx_refCDict(_ctx, _dict);

	_in.size = o->bufsize;
	_out.size = ZSTD_compressBound(o->bufsize);


	_in.buf = malloc(o->bufsize);
	if (!_in.buf) {
		errmsg = EMSG_BUFALLOC;
		goto free;
	}

	_out.buf = malloc(_out.size);
	if (!_out.buf) {
		errmsg = EMSG_BUFALLOC;
		goto free_buf;
	}

	_in.pos = 0;
	_out.pos = 0;

	_timeout_ms = o->timeout_ms;
	timeout.cb = timeout_flush;

	return 0;

free_buf:
	free(_in.buf);
free:
	ZSTD_freeCDict(_dict);
error:
	fprintf(stderr, "Could not initialize zstd compression: %s\n", errmsg);
	return -1;
}

void
zstd_stop(bool flush)
{
	if (flush)
		zstd_compress_and_flush();

	free(_in.buf);
	free(_out.buf);
	ZSTD_freeCDict(_dict);
}

