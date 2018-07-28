#include "raid.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h> /* superset of previous */
#include <unistd.h>
#include <arpa/inet.h>
#include "vector.h"

struct client_config
{
	char error_log[MAX_PATH_LENGTH];
	char cache_size[16];
	char cache_replacement[16];
	int timeout;
};

struct raid_storage
{
	int raid;
	char disk_name[MAX_NAME_LENGTH];
	char mount_point[MAX_PATH_LENGTH];
	char hot_swap[MAX_IP_LENGTH];
	vector servers;
};

void parse_config(const char *config_file, vector *storages, struct client_config *config)
{
	FILE *file = fopen(config_file, "r");
	if (file == NULL)
	{
		fprintf(stderr, "%s\n", "Can't read provided file");
		exit(EXIT_FAILURE);
	}
	char line[256];

	/* Error log. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	strncpy(config->error_log, line + strlen("errorlog = "), strlen(line) - strlen("errorlog = ") + 1);

	/* Cache size. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	strncpy(config->cache_size, line + strlen("cache_size = "), strlen(line) - strlen("cache_size = ") + 1);

	/* Cache replacement algorithm. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	strncpy(config->cache_replacement, line + strlen("cache_replacment = "), strlen(line) - strlen("cache_replacment = ") + 1);

	/* Timeout. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	config->timeout = atoi(line + strlen("timeout = "));

	while (fgets(line, sizeof(line), file))
	{
		/* Remove trailing newline character if it exists. */
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[--len] = '\0';

		/* Disk name. */
		if (strncmp(line, "diskname", strlen("diskname")) == 0)
		{
			struct raid_storage *storage = malloc(sizeof(struct raid_storage));
			vector_new(&storage->servers, MAX_IP_LENGTH, NULL, 2);

			strncpy(storage->disk_name, line + strlen("diskname = "), strlen(line) - strlen("diskname = "));
			vector_append(storages, storage);
		}

		/* Mount point. */
		if (strncmp(line, "mountpoint", strlen("mountpoint")) == 0)
		{
			struct raid_storage *storage = vector_last(storages);
			strncpy(storage->mount_point, line + strlen("mountpoint = "), strlen(line) - strlen("mountpoint = "));
		}

		/* Hotswap. */
		if (strncmp(line, "hotswap", strlen("hotswap")) == 0)
		{
			struct raid_storage *storage = vector_last(storages);
			strncpy(storage->hot_swap, line + strlen("hotswap = "), strlen(line) - strlen("hotswap = "));
		}

		/* RAID. */
		if (strncmp(line, "raid", strlen("raid")) == 0)
		{
			struct raid_storage *storage = vector_last(storages);
			storage->raid = atoi(line + strlen("raid = "));
		}

		/* Server list. */
		if (strncmp(line, "servers", strlen("servers")) == 0)
		{
			struct raid_storage *storage = vector_last(storages);
			char *ip = strtok(line + strlen("servers = "), ", ");
			while (ip != NULL)
			{
				vector_append(&storage->servers, strdup(ip));
				ip = strtok(NULL, ", ");
			}
		}
	}

	fclose(file);
}

void print_fn(void *elem, void *aux)
{
	fprintf(stdout, "%s\n", (char *)elem);
}

static int send_data(struct request request, void *buffer, size_t size)
{
	int sfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5000);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	write(sfd, &request, sizeof(request));

	int status = -errno;
	if (buffer != NULL)
	{
		struct response response;
		read(sfd, &response, sizeof(struct response));
		status = response.status;
		memcpy(buffer, response.data, size);
	}
	else
		read(sfd, &status, sizeof(status));

	close(sfd);
	return status;
}

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "getattr", path);
	struct request request;
	request.syscall = sys_getattr;
	strcpy(request.path, path);

	int status = send_data(request, (void *)stbuf, sizeof(struct stat));
	// TODO: აქ არ მუშაობს რაღაც.

	// printf("Getattr status: %d\n", status);
	// printf("Gettatr uid: %d\n", stbuf->st_uid);
	// printf("Gettatr size: %d\n", stbuf->st_size);

	return status;
}

static int net_mknod(const char *path, mode_t mode, dev_t dev)
{
	printf("%s\n", "mknod");
	struct request request;
	request.syscall = sys_mknod;
	strcpy(request.path, path);
	request.mode = mode;
	request.dev = dev;

	return send_data(request, NULL, 0);
}

static int net_mkdir(const char *path, mode_t mode)
{
	printf("%s\n", "mkdir");
	struct request request;
	request.syscall = sys_mkdir;
	strcpy(request.path, path);
	request.mode = mode;

	return send_data(request, NULL, 0);
}

static int net_unlink(const char *path)
{
	printf("%s\n", "unlink");
	struct request request;
	request.syscall = sys_unlink;
	strcpy(request.path, path);

	return send_data(request, NULL, 0);
}

static int net_rmdir(const char *path)
{
	printf("%s\n", "rmdir");
	struct request request;
	request.syscall = sys_rmdir;
	strcpy(request.path, path);

	return send_data(request, NULL, 0);
}

static int net_rename(const char *path, const char *new_path)
{
	printf("%s\n", "rename");
	struct request request;
	request.syscall = sys_rename;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return send_data(request, NULL, 0);
}

static int net_link(const char *path, const char *new_path)
{
	printf("%s\n", "link");
	struct request request;
	request.syscall = sys_link;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return send_data(request, NULL, 0);
}

static int net_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	printf("%s\n", "chmod");

	struct request request;
	request.syscall = sys_chmod;
	strcpy(request.path, path);
	request.mode = mode;
	request.fi = *fi;

	return send_data(request, NULL, 0);
}

static int net_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	printf("%s\n", "truncate");

	struct request request;
	request.syscall = sys_truncate;
	strcpy(request.path, path);
	request.offset = offset;
	request.fi = *fi;

	return send_data(request, NULL, 0);
}

static int net_open(const char *path, struct fuse_file_info *fi)
{
	printf("%s\n", "open");

	struct request request;
	request.syscall = sys_open;
	strcpy(request.path, path);
	request.fi = *fi;

	return send_data(request, NULL, 0);
}

static int read_write(struct request request, void *read_buffer, const void *write_buffer)
{
	int sfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(5000);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	write(sfd, &request, sizeof(request));

	int status = -errno;
	size_t size = 0;
	if (read_buffer != NULL)
	{
		struct rw_response response;
		read(sfd, &response, sizeof(struct rw_response));
		status = response.status;
		size = response.size;
		read(sfd, read_buffer, size);
	}
	else if (write_buffer != NULL)
	{
		write(sfd, write_buffer, request.size);
		struct rw_response response;
		read(sfd, &response, sizeof(struct rw_response));
		status = response.status;
	}

	close(sfd);
	return status;
}

static int net_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("%s\n", "read");

	struct request request;
	request.syscall = sys_read;
	strcpy(request.path, path);
	request.size = size;
	request.offset = offset;
	request.fi = *fi;

	return read_write(request, (void *)buffer, NULL);
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("%s\n", "write");

	struct request request;
	request.syscall = sys_write;
	strcpy(request.path, path);
	request.size = size;
	request.offset = offset;
	request.fi = *fi;

	// return read_write(request, NULL, buffer);
	return 0;
}

static int net_statfs(const char *path, struct statvfs *statv) { return 0; }

static int net_flush(const char *path, struct fuse_file_info *fi) { return 0; }

static int net_release(const char *path, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "release", path);

	struct request request;
	request.syscall = sys_release;
	strcpy(request.path, path);
	request.fi = *fi;

	// return send_data(request, NULL, 0);
}

static int net_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) { return 0; }

static int net_getxattr(const char *path, const char *name, const char *value, size_t size) { return 0; }

static int net_opendir(const char *path, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "opendir", path);

	struct request request;
	request.syscall = sys_opendir;
	strcpy(request.path, path);
	request.fi = *fi;

	return send_data(request, NULL, 0);
}

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "readdir", path);

	struct request request;
	request.syscall = sys_readdir;
	strcpy(request.path, path);
	request.offset = offset;
	request.fi = *fi;

	char *dirlist = malloc(DATA_SIZE);
	int result = send_data(request, dirlist, DATA_SIZE);

	char *dir = strtok(dirlist, "|");
	while (dir != NULL)
	{
		filler(buffer, dir, NULL, 0);
		dir = strtok(NULL, "|");
	}

	free(dirlist);
	return result;
}

static int net_releasedir(const char *path, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "releasedir", path);

	struct request request;
	request.syscall = sys_releasedir;
	strcpy(request.path, path);
	request.fi = *fi;

	// return send_data(request, NULL, 0);
}

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "create", path);

	struct request request;
	request.syscall = sys_create;
	strcpy(request.path, path);
	request.mode = mode;
	request.fi = *fi;

	return send_data(request, NULL, 0);
}

struct fuse_operations net_oper = {
	.getattr = net_getattr,
	.mknod = net_mknod,
	.mkdir = net_mkdir,
	.unlink = net_unlink,
	.rmdir = net_rmdir,
	.rename = net_rename,
	.link = net_link,
	.chmod = net_chmod,
	.truncate = net_truncate,
	.open = net_open,
	.read = net_read,
	.write = net_write,
	.statfs = net_statfs,
	.flush = net_flush,
	.release = net_release,
	.setxattr = net_setxattr,
	.getxattr = net_getxattr,

	.opendir = net_opendir,
	.readdir = net_readdir,
	.releasedir = net_releasedir,
	// .create = net_create,
};

int main(int argc, char *argv[])
{
	// if (argc <= 1)
	// {
	// 	fprintf(stderr, "%s\n", "Please provide a configuration file");
	// 	exit(EXIT_FAILURE);
	// }

	// vector storages;
	// struct client_config config;

	// vector_new(&storages, sizeof(struct raid_storage), NULL, 2);
	// parse_config((char *)argv[1], &storages, &config);

	// fprintf(stdout, "%s\n", config.error_log);
	// fprintf(stdout, "%s\n", config.cache_size);
	// fprintf(stdout, "%s\n", config.cache_replacement);
	// fprintf(stdout, "%d\n", config.timeout);

	// struct raid_storage *s = vector_last(&storages);
	// fprintf(stdout, "%s\n", s->disk_name);
	// fprintf(stdout, "%s\n", s->mount_point);
	// fprintf(stdout, "%s\n", s->hot_swap);
	// fprintf(stdout, "%d\n", s->raid);
	// vector_map(&s->servers, &print_fn, NULL);

	// net_opendir("tazuna/123/goch", NULL);
	// net_getattr("tazuna/123/goch", NULL, 7);
	// net_open("tazuna/123/goch", 7);
	// net_readdir("tazuna/123/goch", NULL, NULL, 5, 7);

	return fuse_main(argc, argv, &net_oper, NULL);
}