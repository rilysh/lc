/*
 * BSD 2-Clause License
 *
 * Copyright (c) 2024, rilysh
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <wchar.h>
#include <wctype.h>

/* Macros */
#if defined (__GNUC__) || defined (__clang__)
#  define unlikely(x)    __builtin_expect(!!(x), 0)
#else
#  define unlikely(x)   (x)
#endif
#ifndef __dead2
#  define __dead2        __attribute__((noreturn))
#endif

#define is_ascii(x)     (!(x & ~0x7f))
#define is_print(x)     (((unsigned int)x - 0x20) < 0x5f)
#define is_space(x)     ((x == ' ' || ((x - '\t') < 5)))

#undef MAXSIZE
#define MAXSIZE    65205

/* Structure that will contains all our information. */
struct lc_info {
	/* Initial values */
        uintmax_t num_bytes;
        uintmax_t num_lines;
	uintmax_t num_words;

	/* Total values */
	uintmax_t lines_total;
	uintmax_t words_total;
	uintmax_t bytes_total;
};

/* Configration structure. */
struct lc_conf {
	int opt_bytes;
        int opt_lines;
	int opt_words;
};

/* Count bytes, lines, and words.
   This is slower as it has to do all the three
   counting in a same loop.

   But is faster than calling each specific functions
   seperately as they've to iterate over and over again. */
static void count_all(struct lc_info *li, int fd)
{
	struct stat st = {0};
	mbstate_t mst = {0};
	size_t wlen;
	char *buf, *p;
	int have_space;
        wchar_t pwc;
	ssize_t bytes_read;
	uintmax_t sum_bytes;

	sum_bytes = (uintmax_t)0;
	have_space = 1;
	li->num_bytes = li->num_lines = li->num_words = 0;
	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "fstat()");

	/* Check for regular file and size. */
	if (S_ISREG(st.st_mode) && st.st_size > 0)
		li->num_bytes = (uintmax_t)st.st_size;

	/* Use dynamic memory allocation. Under some architectures
	   and libc implementations, a static array such huge is likely
	   prone to blow up the stack. */
	buf = malloc(MAXSIZE);
	if (buf == NULL)
		err(EXIT_FAILURE, "malloc()");

	while ((bytes_read = read(fd, buf, MAXSIZE)) != 0) {
		p = buf;
		sum_bytes += (uintmax_t)bytes_read;

		while (bytes_read > 0) {
			if (is_ascii(*p) && is_print(*p)) {
			        if (is_space(*p)) {
					have_space = 1;
				} else if (have_space) {
					have_space = 0;
					li->num_words++;
				} else if (*p == '\n') {
					li->num_lines++;
				}

				/* It's only a single character. */
				bytes_read--;
				p++;
			} else {
				pwc = (unsigned char)*p;
				wlen = mbrtowc(&pwc, p, (size_t)bytes_read, &mst);
				if (wlen == (size_t)0 ||
				    wlen == (size_t)-1 ||
				    unlikely(wlen == (size_t)-2))
					wlen = 1;

				bytes_read -= wlen;
				p += wlen;

				if (pwc == L'\n')
					li->num_lines++;

				if (iswspace((wint_t)pwc)) {
					have_space = 1;
				} else if (have_space) {
					have_space = 0;
					li->num_words++;
				}
			}
		}
	}

	/* If stat() isn't possible for that file,
	   use our pre-calculated sum. */
	if (li->num_bytes == 0)
		li->num_bytes = (uintmax_t)sum_bytes;

	free(buf);
}


/* Count characters. */
static void count_bytes(struct lc_info *li, int fd)
{
	struct stat st;
	ssize_t bytes_read;
	char *buf;

	li->num_bytes = 0;
	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "fstat()");

	if (S_ISREG(st.st_mode) && st.st_size > 0) {
		li->num_bytes = (uintmax_t)st.st_size;
	} else {
		buf = malloc(MAXSIZE);
		if (buf == NULL)
			err(EXIT_FAILURE, "malloc()");

		while ((bytes_read = read(fd, buf, MAXSIZE)) != 0)
			li->num_bytes += (uintmax_t)bytes_read;

		free(buf);
	}

	close(fd);
}

/* Count lines and characters in a one single loop. */
static void count_lines_and_bytes(struct lc_info *li, int fd)
{
	struct stat st = {0};
	int expect_long;
	ssize_t bytes_read;
        char *buf, *p, *last;
	uintmax_t sum_bytes;

	buf = malloc(MAXSIZE);
	if (buf == NULL)
		err(EXIT_FAILURE, "malloc()");

	li->num_bytes = 0;
	if (fstat(fd, &st) == -1)
		err(EXIT_FAILURE, "fstat()");

	if (S_ISREG(st.st_mode) && st.st_size > 0)
		li->num_bytes = (uintmax_t)st.st_size;
	
        li->num_lines = expect_long = (uintmax_t)0;
	sum_bytes = (uintmax_t)0;

	while ((bytes_read = read(fd, buf, MAXSIZE)) != 0) {
		p = buf;
	        sum_bytes += (uintmax_t)bytes_read;

		/* If we exceed the threshold, we're
		   dealing with a large file. */
		if (expect_long) {
			last = p + bytes_read;
			*last = '\n';

			for (; (p = memchr(p, '\n', (size_t)-1)) < last; p++)
			        li->num_lines++;
		} else {
			/* Threshold to expect when the file stream
			   is much larger than the usual, so we can
			   switch to a different mechanics. */
			expect_long = ((uintmax_t)(bytes_read * 6) <= li->num_lines);
			while (bytes_read--)
				if (*p++ == '\n')
					li->num_lines++;
	        }
	}

	if (li->num_bytes == 0)
		li->num_bytes = (uintmax_t)sum_bytes;

	close(fd);
	free(buf);
}

/* Count words. */
static void count_words(struct lc_info *li, int fd)
{
	mbstate_t mst = {0};
	size_t wlen;
	int have_space = 1;
	ssize_t bytes_read;
	wchar_t pwc;
	char *buf, *p;

	buf = malloc(MAXSIZE);
	if (buf == NULL)
		err(EXIT_FAILURE, "malloc()");

        li->num_words = 0;
        while ((bytes_read = read(fd, buf, MAXSIZE)) != 0) {
		p = buf;
		while (bytes_read > 0) {
			/* Is this a printable ASCII character?
			   If yes, no need to convert it to wide
			   a character. */
			if (is_ascii(*p) && is_print(*p)) {
			        if (is_space(*p)) {
					have_space = 1;
				} else if (have_space) {
					have_space = 0;
					li->num_words++;
				}

				/* It's only a single character. */
				bytes_read--;
				p++;
			} else {
			        pwc = (unsigned char)*p;
				wlen = mbrtowc(&pwc, p, (size_t)bytes_read, &mst);
				if (wlen == (size_t)0 ||
				    wlen == (size_t)-1 ||
				    unlikely(wlen == (size_t)-2))
					wlen = 1;

			        bytes_read -= wlen;
				p += wlen;

				/* Check for wide character space and save the state. */
				if (iswspace((wint_t)pwc)) {
					have_space = 1;
				} else if (have_space) {
					have_space = 0;
					li->num_words++;
				}
			}
		}
	}

	close(fd);
	free(buf);
}

/* Options. */
static void use_opts(struct lc_info *li, struct lc_conf *lc, int fd)
{
	/* Possible options, use one that is faster for
	   a specific task-> */
	if (!lc->opt_lines && !lc->opt_words && !lc->opt_bytes)
		lc->opt_lines = lc->opt_words = lc->opt_bytes = 1;

	/* If option is: -lwc (or similar but shuffled) */
	if (lc->opt_lines && lc->opt_words && lc->opt_bytes) {
		count_all(li, fd);
		fprintf(stdout, "%12ju%12ju%12ju",
			li->num_lines, li->num_words,
			li->num_bytes);

		li->lines_total += li->num_lines;
		li->words_total += li->num_words;
		li->bytes_total += li->num_bytes;
	}

	/* If option is: -c */
	else if (lc->opt_bytes && !lc->opt_lines && !lc->opt_words) {
		count_bytes(li, fd);
		fprintf(stdout, "%12ju", li->num_bytes);

		li->bytes_total += li->num_bytes;
	}

	/* If option is: -l */
	else if (lc->opt_lines && !lc->opt_bytes && !lc->opt_words) {
		count_lines_and_bytes(li, fd);
		fprintf(stdout, "%12ju", li->num_lines);

		li->lines_total += li->num_lines;
	}

	/* If option is: -lc (or similar but shuffled) */
	else if (lc->opt_lines && lc->opt_bytes && !lc->opt_words) {
		count_lines_and_bytes(li, fd);
		fprintf(stdout, "%12ju%12ju", li->num_lines, li->num_bytes);

		li->lines_total += li->num_lines;
		li->bytes_total += li->num_bytes;
	}

	/* If option is: -w */
	else if (lc->opt_words && !lc->opt_lines && !lc->opt_bytes) {
		count_words(li, fd);
		fprintf(stdout, "%12ju", li->num_words);

		li->words_total += li->num_words;
	}

	/* If option is: -lw (or similar but shuffled) */
	else if (lc->opt_lines && lc->opt_words && !lc->opt_bytes) {
		count_all(li, fd);
		fprintf(stdout, "%12ju%12ju",
			li->num_lines, li->num_words);

		li->lines_total += li->num_lines;
		li->words_total += li->num_words;
	}

	/* If option is: -cw (or similar but shuffled) */
	else if (lc->opt_bytes && lc->opt_words && !lc->opt_lines) {
		count_all(li, fd);
		fprintf(stdout, "%12ju%12ju",
			li->num_words, li->num_bytes);

		li->words_total += li->num_words;
		li->bytes_total += li->num_bytes;
	}
}

/* Usage. */
__dead2
static inline void usage(int status)
{
	fputs("usage: [-blwh] [file ...]\n",
	      status == EXIT_FAILURE ? stderr : stdout);
	exit(status);
}

int main(int argc, char **argv)
{
	int fd, opt, is_total = 0;
	char *arg;
	struct lc_info li = {0};
	struct lc_conf lc = {0};

	while ((opt = getopt(argc, argv, "blwh")) != -1) {
		switch (opt) {
		case 'b':
			lc.opt_bytes = 1;
			break;

		case 'l':
			lc.opt_lines = 1;
			break;

		case 'w':
			lc.opt_words = 1;
			break;

		case 'h':
			usage(EXIT_SUCCESS);

		default:
			usage(EXIT_FAILURE);
		}
	}
	argv += optind;
        argc -= optind;

	li.lines_total = li.words_total = li.bytes_total = (uintmax_t)0;
	if (argc == 0) {
		fd = open("/dev/stdin", O_RDONLY);
		if (fd == -1)
			err(EXIT_FAILURE, "open()");
		use_opts(&li, &lc, fd);
		fputc('\n', stdout);
		close(fd);
	} else {
		is_total = argc > 1;
	        while (argc--) {
			arg = *argv++;
			if (*arg == '-' && *(arg + 1) == '\0') {
			        fputs("lc: invalid option prefix.\n", stderr);
			        exit(EXIT_FAILURE);
			}
 
			fd = open(arg, O_RDONLY);
			if (fd == -1)
			        err(EXIT_FAILURE, "%s", arg);

			use_opts(&li, &lc, fd);

			/* Print the file name. */
			fprintf(stdout, " %s\n", arg);
			close(fd);
		}
	}

	if (is_total) {
		/* If option is: -lw (or similar but shuffled) */
	        if (lc.opt_lines && lc.opt_words && !lc.opt_bytes)
			fprintf(stdout, "%12ju%12ju total\n",
				li.lines_total, li.words_total);

		/* If option is: -l */
		else if (lc.opt_lines && !lc.opt_words && !lc.opt_bytes)
			fprintf(stdout, "%12ju total\n",
				li.lines_total);

		/* If option is: -wc (or similar but shuffled) */
		else if (!lc.opt_lines && lc.opt_words && lc.opt_bytes)
			fprintf(stdout, "%12ju%12ju total\n",
				li.words_total, li.bytes_total);

		/* If option is: -c */
		else if (!lc.opt_lines && !lc.opt_words && lc.opt_bytes)
			fprintf(stdout, "%12ju total\n",
				li.bytes_total);

		/* If option is: -lc (or similar but shuffled) */
		else if (lc.opt_lines && !lc.opt_words && lc.opt_bytes)
			fprintf(stdout, "%12ju%12ju total\n",
				li.lines_total, li.bytes_total);

		/* If option is: -w */
		else if (!lc.opt_lines && lc.opt_words && !lc.opt_bytes)
			fprintf(stdout, "%12ju total\n",
				li.words_total);

		else
			fprintf(stdout, "%12ju%12ju%12ju total\n",
				li.lines_total, li.words_total,
				li.bytes_total);
	}
}
