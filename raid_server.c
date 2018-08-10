#include "raid.h"
#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/xattr.h>
#include <string.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <pthread.h>
#include <dirent.h>
#include <sys/sendfile.h>

#define BACKLOG 16

struct server_config
{
	char mount_point[MAX_PATH_LENGTH];
	char ip[MAX_IP_LENGTH];
	int port;
};

struct server_config config;
enum syscalls last_syscall;

static void get_hash(const char *path, char result[MD5_DIGEST_LENGTH * 2])
{
	unsigned char digest[MD5_DIGEST_LENGTH];
	FILE *file = fopen(path, "rb");
	MD5_CTX context;
	int bytes;
	unsigned char data[DATA_SIZE];

	MD5_Init(&context);
	while ((bytes = fread(data, 1, DATA_SIZE, file)) != 0)
		MD5_Update(&context, data, bytes);
	MD5_Final(digest, &context);

	char temp[MD5_DIGEST_LENGTH * 2];
	int i;
	for (i = 0; i < MD5_DIGEST_LENGTH; i++)
	{
		sprintf(temp, "%02x", digest[i]);
		strcat(result, temp);
	}
}

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

	// printf("getattr status: %d\n", result);
	// printf("getattr path: %s\n", path);
	// printf("gettatr uid: %d\n", stbuf->st_uid);
	// printf("gettatr size: %d\n", stbuf->st_size);

	if (result < 0)
		return -errno;

	// close(fd);
	return result;
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
	printf("old: %s\n", path);
	printf("new: %s\n", new_path);
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
	int fd = open(path, fi->flags);
	int result = chmod(path, mode);

	if (result < 0)
		return -errno;

	close(fd);
	return 0;
}

static int net_truncate(const char *path, off_t size, struct fuse_file_info *fi)
{
	(void)fi;
	// int fd = open(path, fi->flags);
	int result = truncate(path, size);

	if (result < 0)
		return -errno;

	// close(fd);
	return 0;
}

static int net_open(const char *path, struct fuse_file_info *fi)
{
	printf("OPEN\n");
	int fd = open(path, fi->flags);

	if (fd < 0)
		return -errno;

	char actual_hash[MD5_DIGEST_LENGTH * 2];
	get_hash(path, actual_hash);
	char saved_hash[MD5_DIGEST_LENGTH * 2];
	getxattr(path, HASH_XATTR, saved_hash, MD5_DIGEST_LENGTH * 2);

	if (strncmp(actual_hash, saved_hash, MD5_DIGEST_LENGTH * 2) != 0)
	{
		printf("Hash mismatch: %s - %s\n", actual_hash, saved_hash);
		return -HASH_MISMATCH;
	}

	close(fd);
	return 0;
}

static int net_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	int result;

	int fd = open(path, fi->flags);
	result = pread(fd, buffer, size, offset);
	if (result < 0)
		return -errno;

	printf("path: %s, size: %zu read: %d\n", path, size, result);
	close(fd);
	return result;
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi, unsigned char request_digest[MD5_DIGEST_LENGTH])
{
	int result;

	printf("path: %s, size: %zu\n", path, size);
	int fd = open(path, fi->flags);
	result = pwrite(fd, buffer, size, offset);

	unsigned char digest[MD5_DIGEST_LENGTH];
	MD5_CTX context;
	MD5_Init(&context);
	MD5_Update(&context, buffer, size);
	MD5_Final(digest, &context);

	// if (result < 0)
	if (result < 0 || (memcmp(digest, request_digest, MD5_DIGEST_LENGTH) != 0))
		return -errno;

	close(fd);
	return result;
}

static int net_setxattr(const char *path, const char *name, const char *value, size_t size, int flags)
{
	int result;

	// printf("path: %s\n name: %s\n value: %s\n size: %zu\n flags: %d\n", path, name, value, size, flags);

	result = setxattr(path, name, value, size, flags);
	if (result < 0)
		return -errno;

	return result;
}

static int net_getxattr(char *path, char *name, char *value, size_t size)
{
	int result;

	result = getxattr(path, name, value, size);
	if (result < 0)
		return -errno;

	return result;
}

// TODO: Can be optimized by calling MD5_Update on write and MD5_Final here.
static int net_release(const char *path, struct fuse_file_info *fi)
{
	/* If the last syscall before this one was write, that means that
	   the client has finished writing a file and we can calculate its hash. */
	if (last_syscall == sys_write)
	{
		char hash[MD5_DIGEST_LENGTH * 2];
		get_hash(path, hash);

		char name[] = HASH_XATTR;
		net_setxattr(path, name, hash, strlen(hash), 0);

		printf("%s", hash);
		printf(" %s\n", path);
	}

	return 0;
}

static int net_access(const char *path, int mask)
{
	int result;

	result = access(path, mask);
	if (result < 0)
		return -errno;

	return result;
}

#ifdef HAVE_UTIMENSAT
static int net_utimens(const char *path, const struct timespec ts[2],
					   struct fuse_file_info *fi)
{
	(void)fi;
	int result;

	/* don't use utime/utimes since they follow symlinks */
	result = utimensat(0, path, ts, AT_SYMLINK_NOFOLLOW);
	if (result == -1)
		return -errno;

	return 0;
}
#endif

static int net_opendir(const char *path, struct fuse_file_info *fi)
{
	DIR *dp = opendir(path);

	if (dp == NULL)
		return -errno;

	closedir(dp);
	return 0;
}

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	DIR *dp = opendir(path);

	char local_buffer[DATA_SIZE];
	struct dirent *de = readdir(dp);
	if (de == 0)
		return -errno;

	int off = 0;
	char delimiter[2] = "|";
	do
	{
		memcpy((char *)local_buffer + off, de->d_name, strlen(de->d_name));
		off += strlen(de->d_name);
		memcpy((char *)local_buffer + off, &delimiter, 1);
		off++;
	} while ((de = readdir(dp)) != NULL);

	char strterm[2] = "\0";
	memcpy((char *)local_buffer + off - 1, &strterm, sizeof(char));

	if (strlen(local_buffer) > strlen(buffer))
	{
		// printf("Replacing: \n%s \nwith \n%s\n", buffer, local_buffer);
		strcpy(buffer, local_buffer);
	}

	closedir(dp);
	return 0;
}

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	int result;

	result = open(path, fi->flags, mode);
	if (result < 0)
		return -errno;

	close(result);
	return 0;
}

/* Send the file located at the given path to the given server. */
static void net_restore_send(const char *path, const char *fpath, char ip[MAX_IP_LENGTH], int port)
{
	printf("vugzavnit ufrosoi %s:%d\n", ip, port);
	int sfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port);
	addr.sin_addr.s_addr = inet_addr(ip);

	connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	// Get file size.
	struct stat stbuf;
	stat(fpath, &stbuf);

	// Create request.
	struct request request;
	request.syscall = sys_restore_receive;
	request.size = stbuf.st_size;
	request.mode = stbuf.st_mode;
	strcpy(request.path, path);

	// Send the request.
	write(sfd, &request, sizeof(request));

	// Send the file.
	int fd = open(fpath, O_RDWR);
	sendfile(sfd, fd, 0, stbuf.st_size);
}

static void net_restore_receive(const char *path, size_t size, mode_t mode, int cfd)
{
	// Read the data from socket.
	char buffer[size];
	read(cfd, buffer, size);

	printf("ebeeeee\n");

	int fd = open(path, O_RDWR);
	if (fd == -1)
	{
		fd = open(path, O_CREAT | O_RDWR);
	}
	write(fd, buffer, size);

	char hash[MD5_DIGEST_LENGTH * 2];
	get_hash(path, hash);

	char name[] = HASH_XATTR;
	net_setxattr(path, name, hash, strlen(hash), 0);

	struct fuse_file_info fi;
	fi.flags = O_RDWR;
	net_chmod(path, mode, &fi);

	close(fd);
}

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

		char fullpath[strlen(request.path) + strlen(config.mount_point) + 1];
		strncpy(fullpath, config.mount_point, strlen(config.mount_point) - 1);
		strcat(fullpath, request.path);

		switch (request.syscall)
		{
		case sys_getattr:
		{
			struct response response;
			response.status = net_getattr(fullpath, (struct stat *)&response.data, &request.fi);
			write(cfd, &response, sizeof(struct response));
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
			char fullpath_new[strlen(request.new_path) + strlen(config.mount_point) + 1];
			strncpy(fullpath_new, config.mount_point, strlen(config.mount_point) - 1);
			strcat(fullpath_new, request.new_path);

			result = net_rename(fullpath, fullpath_new);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_link:
		{
			char fullpath_new[strlen(request.new_path) + strlen(config.mount_point) + 1];
			strncpy(fullpath_new, config.mount_point, strlen(config.mount_point) - 1);
			strcat(fullpath_new, request.new_path);

			result = net_link(fullpath, fullpath_new);
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

			char *buffer = malloc(response.size);
			response.status = net_read(fullpath, buffer, request.size, request.offset, &request.fi);
			write(cfd, &response, sizeof(struct rw_response));
			write(cfd, buffer, response.size);

			// printf("Should read: %d + %d = %d\n", sizeof(struct rw_response), response.size, sizeof(struct rw_response) + response.size);

			free(buffer);
			break;
		}
		case sys_write:
		{
			struct rw_response response;

			char *buffer = malloc(request.size);
			read(cfd, buffer, request.size);

			// printf("%s\n", "write");
			// printf("buff: %s\n", buffer);
			// printf("len: %d\n", strlen((char *)buffer));
			// printf("size: %d\n", request.size);

			// int i;
			// printf("\nGiven hash: ");
			// for (i = 0; i < MD5_DIGEST_LENGTH; i++)
			// 	printf("%02x", request.digest[i]);
			// printf("\n");

			// response.status = 0;
			response.status = net_write(fullpath, buffer, request.size, request.offset, &request.fi, (unsigned char *)&request.digest);
			write(cfd, &response, sizeof(struct rw_response));

			free(buffer);
			break;
		}
		case sys_statfs:
			break;
		case sys_flush:
			break;
		case sys_release:
		{
			result = net_release(fullpath, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_setxattr:
			break;
		case sys_getxattr:
			break;
		case sys_access:
		{
			result = net_access(fullpath, request.mask);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_utimens:
		{
			break;
		}
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
		case sys_create:
		{
			result = net_create(fullpath, request.mode, &request.fi);
			write(cfd, &result, sizeof(result));
			break;
		}
		case sys_restore_send:
		{
			net_restore_send(request.path, fullpath, request.ip, request.port);
			break;
		}
		case sys_restore_receive:
		{
			net_restore_receive(fullpath, request.size, request.mode, cfd);
			break;
		}
		default:
			result = -errno;
			write(cfd, &result, sizeof(result));
			break;
		}

		last_syscall = request.syscall;
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
		printf("Accepted incoming connection...\n");

		pthread_t p;
		pthread_create(&p, NULL, &client_handler, &cfd);
	}
	close(sfd);

	return 0;
}
