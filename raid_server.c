#include "raid.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>

#define BACKLOG 16

struct server_config
{
	char mount_point[MAX_PATH_LENGTH];
	char ip[MAX_IP_LENGTH];
	int port;
};

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	int result = 0;

	memset(stbuf, 0, sizeof(struct stat));
	if (strcmp(path, "/") == 0)
	{
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
	}
	else
		result = lstat(path, stbuf);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_mknod(const char *path, mode_t mode, dev_t dev)
{
	int result = mknod(path, mode, dev);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_mkdir(const char *path, mode_t mode)
{
	int result = mkdir(path, mode);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_unlink(const char *path)
{
	int result = unlink(path);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_rmdir(const char *path)
{
	int result = rmdir(path);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_rename(const char *path, const char *new_path)
{
	int result = rename(path, new_path);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_link(const char *path, const char *new_path)
{
	int result = link(path, new_path);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	(void)fi;
	int result = chmod(path, mode);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi)
{
	(void)fi;
	int result = chown(path, uid, gid);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	(void)fi;
	int result = truncate(path, offset);

	if (result < 0)
		return -errno;

	return 0;
}

static int net_open(const char *path, struct fuse_file_info *fi)
{
	int fd = open(path, fi->flags);

	if (fd < 0)
		return -errno;

	fi->fh = fd;

	return 0;
}

static int net_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int result;

	/* We can just call pread with fi->fh because at this point open syscall should be called and fh will be set. */
	result = pread(fi->fh, buffer, size, offset);
	if (result < 0)
		return -errno;
	return 0;
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int result;

	/* We can just call pwrite with fi->fh because at this point open syscall should be called and fh will be set. */
	result = pwrite(fi->fh, buffer, size, offset);
	if (result < 0)
		return -errno;

	return 0;
}

static int net_statfs(const char *path, struct statvfs *statv) { return 0; }

static int net_flush(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_release(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) { return 0; }

static int net_getxattr(const char *path, const char *name, const char *value, size_t size) { return 0; }

static int net_opendir(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) { return 0; }

static int net_releasedir(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi) { return 0; }

static void net_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {}

void *client_handler(void *cf)
{
	int cfd = *(int *)cf;
	struct request req;

	while (1)
	{
		size_t data_size = read(cfd, &req, 1024);
		if (data_size <= 0)
			break;
		printf("%s\n", (char *)req.syscall);
		// write(cfd, &buf, data_size);
	}
	close(cfd);

	return NULL;
}

int main(int argc, char *argv[])
{
	if (argc <= 3)
	{
		fprintf(stderr, "%s\n", "Usage: ./server [ip] [port] [dir]");
		exit(EXIT_FAILURE);
	}

	struct server_config config;
	strcpy(config.ip, argv[1]);
	strcpy(config.mount_point, argv[3]);
	config.port = atoi(argv[2]);

	fprintf(stdout, "%s\n", "Starting up a server...");
	fprintf(stdout, "%s\n", config.ip);
	fprintf(stdout, "%s\n", config.mount_point);
	fprintf(stdout, "%d\n", config.port);

	int sfd, cfd;

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(config.port);
	addr.sin_addr.s_addr = inet_addr(config.ip);

	struct sockaddr_in peer_addr;
	int optval = 1;

	sfd = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(int));
	bind(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	listen(sfd, BACKLOG);

	while (1)
	{
		socklen_t size = sizeof(struct sockaddr_in);
		cfd = accept(sfd, (struct sockaddr *)&peer_addr, &size);

		pthread_t p;
		pthread_create(&p, NULL, &client_handler, &cfd);
	}
	close(sfd);

	return 0;
}