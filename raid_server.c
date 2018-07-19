#include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "vector.h"

#define MAX_NAME_LENGTH 128
#define MAX_PATH_LENGTH 256
#define MAX_IP_LENGTH 24 // 255.255.255.255:65535 (22)

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

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi) {}

static int net_mknod(const char *path, mode_t mode, dev_t dev) {}

static int net_mkdir(const char *path, mode_t mode) {}

static int net_unlink(const char *path) {}

static int net_rmdir(const char *path) {}

static int net_rename(const char *path, const char *new_path) {}

static int net_link(const char *path, const char *new_path) {}

static int net_chmod(const char *path, mode_t mode, struct fuse_file_info *fi) {}

static int net_chown(const char *path, uid_t uid, gid_t gid, struct fuse_file_info *fi) {}

static int net_truncate(const char *path, off_t offset, struct fuse_file_info *fi) {}

static int net_open(const char *path, struct fuse_file_info *fi) {}

static int net_read(const char *path, struct fuse_file_info *fi) {}

static int net_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {}

static int net_statfs(const char *path, struct statvfs *statv) {}

static int net_flush(const char *path, struct fuse_file_info *fi) {}

static int net_release(const char *path, struct fuse_file_info *fi);

static int net_setxattr(const char *path, const char *name, const char *value, size_t size, int flags) {}

static int net_getxattr(const char *path, const char *name, const char *value, size_t size) {}

static int net_opendir(const char *path, struct fuse_file_info *fi) {}

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi) {}

static int net_releasedir(const char *path, struct fuse_file_info *fi) {}

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi) {}

static void net_init(struct fuse_conn_info *conn, struct fuse_config *cfg)
{
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
	.chown = net_chown,
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
	.create = net_create,
	.init = net_init,
};

int main(int argc, char const *argv[])
{
	if (argc <= 1)
	{
		fprintf(stderr, "%s\n", "Please provide a configuration file");
		exit(EXIT_FAILURE);
	}

	vector storages;
	struct client_config config;

	vector_new(&storages, sizeof(struct raid_storage), NULL, 2);
	parse_config((char *)argv[1], &storages, &config);

	fprintf(stdout, "%s\n", config.error_log);
	fprintf(stdout, "%s\n", config.cache_size);
	fprintf(stdout, "%s\n", config.cache_replacement);
	fprintf(stdout, "%d\n", config.timeout);

	struct raid_storage *s = vector_last(&storages);
	fprintf(stdout, "%s\n", s->disk_name);
	fprintf(stdout, "%s\n", s->mount_point);
	fprintf(stdout, "%s\n", s->hot_swap);
	fprintf(stdout, "%d\n", s->raid);
	vector_map(&s->servers, &print_fn, NULL);

	return 0;
}