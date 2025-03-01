/*
  FUSE: Filesystem in Userspace
  Copyright (C) 2001-2007  Miklos Szeredi <miklos@szeredi.hu>

  This program can be distributed under the terms of the GNU GPLv2.
  See the file COPYING.
*/

/** @file
 *
 * minimal example filesystem using low-level API
 *
 * Compile with:
 *
 *     gcc -Wall hello_ll.c `pkg-config fuse3 --cflags --libs` -o hello_ll
 *
 * ## Source code ##
 * \include hello_ll.c
 */

#define FUSE_USE_VERSION 34

#include "fuse_lowlevel.h"
#include "fuse_opt.h"
#include <stdio.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <fuse.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/types.h>

static const char *hello_str = "Hello World!\n";
static const char *hello_name = "hello";

// static const char *socket_path = "/var/run/fd-fuse.sock";
static const char *socket_path = "./fd-fuse.sock";
static const unsigned int nIncomingConnections = 5;

static struct options {
	const char *filename;
	const char *contents;
	const char *mode;
	int show_help;
} options;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt option_spec[] = {
	OPTION("--name=%s", filename),
	OPTION("--contents=%s", contents),
	OPTION("--mode=%s", mode),
	OPTION("-h", show_help),
	OPTION("--help", show_help),
	FUSE_OPT_END
};

static int hello_stat(fuse_ino_t ino, struct stat *stbuf)
{
	stbuf->st_ino = ino;
	switch (ino) {
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;

	case 2:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = strlen(hello_str);
		break;

	default:
		return -1;
	}
	return 0;
}

static void hello_ll_getattr(fuse_req_t req, fuse_ino_t ino,
			     struct fuse_file_info *fi)
{
	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (hello_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void hello_ll_lookup(fuse_req_t req, fuse_ino_t parent, const char *name)
{
	struct fuse_entry_param e;

	if (parent != 1 || strcmp(name, hello_name) != 0)
		fuse_reply_err(req, ENOENT);
	else {
		memset(&e, 0, sizeof(e));
		e.ino = 2;
		e.attr_timeout = 1.0;
		e.entry_timeout = 1.0;
		hello_stat(e.ino, &e.attr);

		fuse_reply_entry(req, &e);
	}
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name,
		       fuse_ino_t ino)
{
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf,
			  b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize,
			     off_t off, size_t maxsize)
{
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void hello_ll_readdir(fuse_req_t req, fuse_ino_t ino, size_t size,
			     off_t off, struct fuse_file_info *fi)
{
	(void) fi;

	if (ino != 1)
		fuse_reply_err(req, ENOTDIR);
	else {
		struct dirbuf b;

		memset(&b, 0, sizeof(b));
		dirbuf_add(req, &b, ".", 1);
		dirbuf_add(req, &b, "..", 1);
		dirbuf_add(req, &b, hello_name, 2);
		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}
}

static void hello_ll_open(fuse_req_t req, fuse_ino_t ino,
			  struct fuse_file_info *fi)
{
	if (ino != 2)
		fuse_reply_err(req, EISDIR);
	else if ((fi->flags & O_ACCMODE) != O_RDONLY)
		fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void hello_ll_read(fuse_req_t req, fuse_ino_t ino, size_t size,
			  off_t off, struct fuse_file_info *fi)
{
	(void) fi;

	assert(ino == 2);
	reply_buf_limited(req, hello_str, strlen(hello_str), off, size);
}

static void hello_ll_getxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  size_t size)
{
	(void)size;
	assert(ino == 2);
	if (strcmp(name, "hello_ll_getxattr_name") == 0)
	{
		const char *buf = "hello_ll_getxattr_value";
		fuse_reply_buf(req, buf, strlen(buf));
	}
	else
	{
		fuse_reply_err(req, ENOTSUP);
	}
}

static void hello_ll_setxattr(fuse_req_t req, fuse_ino_t ino, const char *name,
							  const char *value, size_t size, int flags)
{
	(void)flags;
	(void)size;
	assert(ino == 2);
	const char* exp_val = "hello_ll_setxattr_value";
	if (strcmp(name, "hello_ll_setxattr_name") == 0 &&
	    strlen(exp_val) == size &&
	    strncmp(value, exp_val, size) == 0)
	{
		fuse_reply_err(req, 0);
	}
	else
	{
		fuse_reply_err(req, ENOTSUP);
	}
}

static void hello_ll_removexattr(fuse_req_t req, fuse_ino_t ino, const char *name)
{
	assert(ino == 2);
	if (strcmp(name, "hello_ll_removexattr_name") == 0)
	{
		fuse_reply_err(req, 0);
	}
	else
	{
		fuse_reply_err(req, ENOTSUP);
	}
}

static const struct fuse_lowlevel_ops hello_ll_oper = {
	.lookup = hello_ll_lookup,
	.getattr = hello_ll_getattr,
	.readdir = hello_ll_readdir,
	.open = hello_ll_open,
	.read = hello_ll_read,
	.setxattr = hello_ll_setxattr,
	.getxattr = hello_ll_getxattr,
	.removexattr = hello_ll_removexattr,
};

static void *xrealloc(void *oldptr, size_t size)
{
	void *ptr = realloc(oldptr, size);
	if (!ptr) {
		fprintf(stderr, "failed to allocate memory\n");
		exit(1);
	}
	return ptr;
}

static char *add_option(const char *opt, char *options)
{
	int oldlen = options ? strlen(options) : 0;

	options = xrealloc(options, oldlen + 1 + strlen(opt) + 1);
	if (!oldlen)
		strcpy(options, opt);
	else {
		strcat(options, ",");
		strcat(options, opt);
	}
	return options;
}

static int prepare_fuse_fd(const char *mountpoint, const char* subtype,
			   const char *options)
{
	int fuse_fd = -1;
	int flags = -1;
	int subtype_len = strlen(subtype) + 9;
	char* options_copy = xrealloc(NULL, subtype_len);

	snprintf(options_copy, subtype_len, "subtype=%s", subtype);
	options_copy = add_option(options, options_copy);
	fuse_fd = fuse_open_channel(mountpoint, options_copy);
	if (fuse_fd == -1) {
		exit(1);
	}

	flags = fcntl(fuse_fd, F_GETFD);
	if (flags == -1 || fcntl(fuse_fd, F_SETFD, flags & ~FD_CLOEXEC) == 1) {
		fprintf(stderr, "Failed to clear CLOEXEC: %s\n",
			strerror(errno));
		exit(1);
	}

	return fuse_fd;
}

static int send_fd(int sock_fd, int fd)
{
	int retval;
	struct msghdr msg;
	struct cmsghdr *p_cmsg;
	struct iovec vec;
	size_t cmsgbuf[CMSG_SPACE(sizeof(fd)) / sizeof(size_t)];
	int *p_fds;
	char sendchar = 0;

	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	p_cmsg = CMSG_FIRSTHDR(&msg);
	p_cmsg->cmsg_level = SOL_SOCKET;
	p_cmsg->cmsg_type = SCM_RIGHTS;
	p_cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
	p_fds = (int *) CMSG_DATA(p_cmsg);
	*p_fds = fd;
	msg.msg_controllen = p_cmsg->cmsg_len;
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &vec;
	msg.msg_iovlen = 1;
	msg.msg_flags = 0;
	/* "To pass file descriptors or credentials you need to send/read at
	 * least one byte" (man 7 unix) */
	vec.iov_base = &sendchar;
	vec.iov_len = sizeof(sendchar);
	while ((retval = sendmsg(sock_fd, &msg, 0)) == -1 && errno == EINTR);
	if (retval != 1) {
		perror("sending file descriptor");
		return -1;
	}
	return 0;
}

static int receive_fd(int fd)
{
	struct msghdr msg;
	struct iovec iov;
	char buf[1];
	int rv;
	size_t ccmsg[CMSG_SPACE(sizeof(int)) / sizeof(size_t)];
	struct cmsghdr *cmsg;

	iov.iov_base = buf;
	iov.iov_len = 1;

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = 0;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	/* old BSD implementations should use msg_accrights instead of
	 * msg_control; the interface is different. */
	msg.msg_control = ccmsg;
	msg.msg_controllen = sizeof(ccmsg);

	while(((rv = recvmsg(fd, &msg, 0)) == -1) && errno == EINTR);
	if (rv == -1) {
		perror("recvmsg");
		return -1;
	}
	if(!rv) {
		/* EOF */
		return -1;
	}

	cmsg = CMSG_FIRSTHDR(&msg);
	if (cmsg->cmsg_type != SCM_RIGHTS) {
		fuse_log(FUSE_LOG_ERR, "got control message of unknown type %d\n",
			cmsg->cmsg_type);
		return -1;
	}
	return *(int*)CMSG_DATA(cmsg);
}

int start_fd_server(int fd) {
	//create server side
	int s = 0;
	int s2 = 0;
	struct sockaddr_un local, remote;
	int len = 0;

	s = socket(AF_UNIX, SOCK_STREAM, 0);
	if( -1 == s )
	{
		printf("Error on socket() call \n");
		return 1;
	}

	local.sun_family = AF_UNIX;
	strcpy( local.sun_path, socket_path );
	unlink(local.sun_path);
	len = strlen(local.sun_path) + sizeof(local.sun_family);
	if( bind(s, (struct sockaddr*)&local, len) != 0)
	{
		printf("Error on binding socket \n");
		return 1;
	}

	if( listen(s, nIncomingConnections) != 0 )
	{
		printf("Error on listen call \n");
	}

    

	int bWaiting = 1;
	while (bWaiting)
	{
		unsigned int sock_len = 0;
		printf("Waiting for connection.... \n");
		if( (s2 = accept(s, (struct sockaddr*)&remote, &sock_len)) == -1 )
		{
			printf("Error on accept() call \n");
			return 1;
		}

		printf("Server connected \n");

		int data_recv = 0;
		char recv_buf[100];
		char send_buf[200];
		do{
			memset(recv_buf, 0, 100*sizeof(char));
			memset(send_buf, 0, 200*sizeof(char));
			data_recv = recv(s2, recv_buf, 100, 0);
			if(data_recv > 0)
			{
				printf("Data received: %d : %s \n", data_recv, recv_buf);
				if(send_fd(s2, fd) != 0) {
					printf("Error when sending fd \n");
				}
				// strcpy(send_buf, "Got message: ");
				// strcat(send_buf, recv_buf);

				// if(strstr(recv_buf, "quit")!=0)
				// {
				// 	printf("Exit command received -> quitting \n");
				// 	bWaiting = 0;
				// 	break;
				// }

				// if( send(s2, send_buf, strlen(send_buf)*sizeof(char), 0) == -1 )
				// {
				// 	printf("Error on send() call \n");
				// }
			}
			else
			{
				printf("Error on recv() call \n");
			}
		}while(data_recv > 0);

		close(s2);
	}
}

int connect_fd_server() {
	int sock = 0;
	int data_len = 0;
	struct sockaddr_un remote;
	char recv_msg[100];
	char send_msg[200];

	memset(recv_msg, 0, 100*sizeof(char));
	memset(send_msg, 0, 200*sizeof(char));

	if( (sock = socket(AF_UNIX, SOCK_STREAM, 0)) == -1  )
	{
		printf("Client: Error on socket() call \n");
		return 1;
	}

	remote.sun_family = AF_UNIX;
	strcpy( remote.sun_path, socket_path );
	data_len = strlen(remote.sun_path) + sizeof(remote.sun_family);

	printf("Client: Trying to connect... \n");
	if( connect(sock, (struct sockaddr*)&remote, data_len) == -1 )
	{
		printf("Client: Error on connect call \n");
		return 1;
	}

	printf("Client: Connected \n");
	strcpy(send_msg, "requesting fd");
	if( send(sock, send_msg, strlen(send_msg)*sizeof(char), 0 ) == -1 ) {
		printf("Client: Error on send() call \n");
	}

	return receive_fd(sock);
}

int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config config;
	int ret = -1;

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		fuse_cmdline_help();
		fuse_lowlevel_help();
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if(opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}

	options.filename = strdup("hello");
	options.contents = strdup("Hello World!\n");
	options.mode = strdup("daemon");

	/* Parse options */
	if (fuse_opt_parse(&args, &options, option_spec, NULL) == -1)
		return 1;

	printf("Detecting mode: %s\n", options.mode);
	if (strcmp(options.mode, "daemon") != 0) {
		int fuse_fd = -2;
		char *type = "hell_ll";
		char *options = "rw,nosuid,nodev,relatime";
		fuse_fd = prepare_fuse_fd(opts.mountpoint, type, options);
		// dev_fd_mountpoint = xrealloc(NULL, 19);
		// snprintf(dev_fd_mountpoint, 19, "/dev/fd/%u", fuse_fd);
		// opts.mountpoint = dev_fd_mountpoint;

		printf("Success: prepare_fuse_fd: fd %u, mount point: %s\n", fuse_fd, opts.mountpoint);
		return start_fd_server(fuse_fd);
	}

	char *dev_fd_mountpoint;
	int fuse_fd = connect_fd_server();
	if (fuse_fd < 0) {
		printf("Error on getting fd from fd server\n");
		return 1;
	}
	printf("Success: get fd from fd server, fd=%u\n", fuse_fd);
	dev_fd_mountpoint = xrealloc(NULL, 19);
	snprintf(dev_fd_mountpoint, 19, "/dev/fd/%u", fuse_fd);
	opts.mountpoint = dev_fd_mountpoint;

	se = fuse_session_new(&args, &hello_ll_oper,
			      sizeof(hello_ll_oper), NULL);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
	    goto err_out3;

	fuse_daemonize(opts.foreground);

	/* Block until ctrl+c or fusermount -u */
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config.clone_fd = opts.clone_fd;
		config.max_idle_threads = opts.max_idle_threads;
		ret = fuse_session_loop_mt(se, &config);
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	return ret ? 1 : 0;
}
