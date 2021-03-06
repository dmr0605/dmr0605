/*
 * Copyright (c) 2006 Franck Bui-Huu
 * Copyright (c) 2006 Rene Scharfe
 */
#include "cache.h"
#include "builtin.h"
#include "archive.h"
#include "parse-options.h"
#include "pkt-line.h"
#include "sideband.h"

static void create_output_file(const char *output_file)
{
	int output_fd = open(output_file, O_CREAT | O_WRONLY | O_TRUNC, 0666);
	if (output_fd < 0)
		die_errno("could not create archive file '%s'", output_file);
	if (output_fd != 1) {
		if (dup2(output_fd, 1) < 0)
			die_errno("could not redirect output");
		else
			close(output_fd);
	}
}

static int run_remote_archiver(int argc, const char **argv,
			       const char *remote, const char *exec)
{
	char *url, buf[LARGE_PACKET_MAX];
	int fd[2], i, len, rv;
	struct child_process *conn;

	url = xstrdup(remote);
	conn = git_connect(fd, url, exec, 0);

	for (i = 1; i < argc; i++)
		packet_write(fd[1], "argument %s\n", argv[i]);
	packet_flush(fd[1]);

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (!len)
		die("git archive: expected ACK/NAK, got EOF");
	if (buf[len-1] == '\n')
		buf[--len] = 0;
	if (strcmp(buf, "ACK")) {
		if (len > 5 && !prefixcmp(buf, "NACK "))
			die("git archive: NACK %s", buf + 5);
		die("git archive: protocol error");
	}

	len = packet_read_line(fd[0], buf, sizeof(buf));
	if (len)
		die("git archive: expected a flush");

	/* Now, start reading from fd[0] and spit it out to stdout */
	rv = recv_sideband("archive", fd[0], 1);
	close(fd[0]);
	close(fd[1]);
	rv |= finish_connect(conn);

	return !!rv;
}

static const char *format_from_name(const char *filename)
{
	const char *ext = strrchr(filename, '.');
	if (!ext)
		return NULL;
	ext++;
	if (!strcasecmp(ext, "zip"))
		return "zip";
	return NULL;
}

#define PARSE_OPT_KEEP_ALL ( PARSE_OPT_KEEP_DASHDASH | 	\
			     PARSE_OPT_KEEP_ARGV0 | 	\
			     PARSE_OPT_KEEP_UNKNOWN |	\
			     PARSE_OPT_NO_INTERNAL_HELP	)

int cmd_archive(int argc, const char **argv, const char *prefix)
{
	const char *exec = "git-upload-archive";
	const char *output = NULL;
	const char *remote = NULL;
	const char *format = NULL;
	struct option local_opts[] = {
		OPT_STRING('o', "output", &output, "file",
			"write the archive to this file"),
		OPT_STRING(0, "remote", &remote, "repo",
			"retrieve the archive from remote repository <repo>"),
		OPT_STRING(0, "exec", &exec, "cmd",
			"path to the remote git-upload-archive command"),
		OPT_STRING(0, "format", &format, "fmt", "archive format"),
		OPT_END()
	};
	char fmt_opt[32];

	argc = parse_options(argc, argv, prefix, local_opts, NULL,
			     PARSE_OPT_KEEP_ALL);

	if (output) {
		create_output_file(output);
		if (!format)
			format = format_from_name(output);
	}

	if (format) {
		sprintf(fmt_opt, "--format=%s", format);
		/*
		 * We have enough room in argv[] to muck it in place,
		 * because either --format and/or --output must have
		 * been given on the original command line if we get
		 * to this point, and parse_options() must have eaten
		 * it, i.e. we can add back one element to the array.
		 * But argv[] may contain "--"; we should make it the
		 * first option.
		 */
		memmove(argv + 2, argv + 1, sizeof(*argv) * argc);
		argv[1] = fmt_opt;
		argv[++argc] = NULL;
	}

	if (remote)
		return run_remote_archiver(argc, argv, remote, exec);

	setvbuf(stderr, NULL, _IOLBF, BUFSIZ);

	return write_archive(argc, argv, prefix, 1);
}
