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
#include <dirent.h>

#define BACKLOG 16

struct server_config
{
	char mount_point[MAX_PATH_LENGTH];
	char ip[MAX_IP_LENGTH];
	int port;
};

struct server_config config;

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	int result = 0;

	// memset(stbuf, 0, sizeof(struct stat));
	// if (strcmp(path, "/") == 0)
	// {
	// 	stbuf->st_mode = S_IFDIR | 0755;
	// 	stbuf->st_nlink = 2;
	// }
	// else
	result = lstat(path, stbuf);

	if (result < 0)
		return -errno;

	return result;
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

	// printf("size: %d, offset: %d\n", size, offset);
	int fd = open(path, fi->flags);
	result = pread(fd, buffer, size, offset);
	if (result < 0)
		return -errno;

	// close(fd);
	return result;
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int result;

	int fd = open(path, fi->flags);
	result = pwrite(fd, buffer, size, offset);
	if (result < 0)
		return -errno;

	// close(fd);
	return result;
}

static int net_statfs(const char *path, struct statvfs *statv) { return 0; }

static int net_flush(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_release(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) { return 0; }

static int net_getxattr(const char *path, const char *name, const char *value, size_t size) { return 0; }

static int net_opendir(const char *path, struct fuse_file_info *fi)
{
	DIR *dp = opendir(path);

	if (dp == NULL)
		return -errno;

	// closedir(dp);
	return 0;
}

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	DIR *dp = opendir(path);

	struct dirent *de;
	int off = 0;
	char delimiter[2] = "|";
	while ((de = readdir(dp)) != NULL)
	{
		memcpy((char *)buffer + off, de->d_name, strlen(de->d_name));
		off += strlen(de->d_name);
		memcpy((char *)buffer + off, &delimiter, 1);
		off++;
	}

	char strterm[2] = "\0";
	memcpy((char *)buffer + off - 1, &strterm, sizeof(char));

	// closedir(dp);
	return 0;
}

static int net_releasedir(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi) { return 0; }

static void net_init(struct fuse_conn_info *conn, struct fuse_config *cfg) {}

static void *client_handler(void *cf)
{
	int cfd = *(int *)cf;

	while (1)
	{
		struct request request;
		size_t data_size = read(cfd, &request, sizeof(struct request));
		if (data_size <= 0)
			break;

		printf("Called syscall: %d\n", request.syscall);

		int result = 0;

		char *fullpath = malloc(strlen(request.path) + strlen(config.mount_point) + 1);
		strncpy(fullpath, config.mount_point, strlen(config.mount_point) - 1);
		strcat(fullpath, request.path);

		switch (request.syscall)
		{
		case sys_getattr:
		{
			struct response response;
			response.status = net_getattr(fullpath, &response.data, &request.fi);
			write(cfd, &response, sizeof(struct response));
			break;
		}
		case sys_mknod:
		{
			result = net_mknod(fullpath, request.mode, request.dev);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_mkdir:
		{
			result = net_mkdir(fullpath, request.mode);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_unlink:
		{
			result = net_unlink(fullpath);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_rmdir:
		{
			result = net_rmdir(fullpath);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_rename:
		{
			result = net_rename(fullpath, request.new_path);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_link:
		{
			result = net_link(fullpath, request.new_path);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_chmod:
		{
			result = net_chmod(fullpath, request.mode, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_truncate:
		{
			result = net_truncate(fullpath, request.offset, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_open:
		{
			result = net_open(fullpath, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_read:
		{
			struct rw_response response;
			response.size = request.size;

			void *buffer = malloc(response.size);
			response.status = net_read(fullpath, buffer, request.size, request.offset, &request.fi);
			write(cfd, &response, sizeof(struct rw_response));
			write(cfd, buffer, response.size);

			free(buffer);
			break;
		}
		case sys_write:
		{
			struct rw_response response;
			response.size = request.size;

			char *buffer = malloc(request.size);
			read(cfd, buffer, request.size);
			response.status = net_write(fullpath, buffer, request.size, request.offset, &request.fi);
			write(cfd, &response, sizeof(struct response));

			free(buffer);
			break;
		}
		case sys_statfs:
			break;
		case sys_flush:
			break;
		case sys_release:
			break;
		case sys_setxattr:
			break;
		case sys_getxattr:
			break;
		case sys_opendir:
		{
			result = net_opendir(fullpath, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_readdir:
		{
			struct response response;
			response.status = net_readdir(fullpath, &response.data, NULL, request.offset, &request.fi);
			write(cfd, &response, sizeof(struct response));
			break;
		}
		case sys_releasedir:
			break;
		case sys_create:
		{
			result = net_create(fullpath, request.mode, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		default:
			result = -errno;
			write(cfd, &result, sizeof(result));
			break;
		}
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

	strcpy(config.ip, argv[1]);
	strcpy(config.mount_point, argv[3]);
	config.port = atoi(argv[2]);

	fprintf(stdout, "\n%s\n", "Starting up a server...");
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

// -------------------------------------- //
/* fi-ები შევინახო და release დავწერო ნორმალურად. */