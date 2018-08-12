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
#include <pthread.h>
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

static vector storages;
static int storage_index;
static int server_connections[2];

static void parse_config(const char *config_file, vector *storages, struct client_config *config)
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
			strncpy(storage->mount_point, line + strlen("mountpoint = "), strlen(line) - strlen("mountpoint = ") - 1);
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

static int send_data(struct request request, void *buffer, size_t size, int server)
{
	printf("Connecting to: server%d\n", server);
	size_t s = write(server_connections[server], &request, sizeof(request));

	int status = -errno;
	if (buffer != NULL)
	{
		struct response response;
		read(server_connections[server], &response, sizeof(struct response));
		status = response.status;
		memcpy(buffer, response.data, size);
	}
	else
		read(server_connections[server], &status, sizeof(status));

	return status;
}

static void call_restore(struct request request, int index)
{
	struct raid_storage *s = vector_nth(&storages, storage_index);
	char *receiver = strdup(vector_nth(&s->servers, !index));
	char *receiver_ip = strsep(&receiver, ":");
	int receiver_port = atoi(strsep(&receiver, ":"));

	printf("Receiver: %s:%d\n", receiver_ip, receiver_port);

	// Copy index -> !index
	struct request send_request;
	send_request.syscall = sys_restore_send;
	strcpy(send_request.path, request.path);
	strcpy(send_request.ip, receiver_ip);
	send_request.port = receiver_port;

	write(server_connections[index], &send_request, sizeof(struct request));
}

static int raid_controller(struct request request, void *buffer, size_t size)
{
	size_t server_count = 2;

	int results[server_count];
	int result_index = 0;

	size_t i;
	for (i = 0; i < server_count; i++)
	{
		if (request.syscall == sys_getattr)
		{
			results[i] = send_data(request, buffer, size, i);
			if (results[i] == 0)
				return 0;
		}
		else if (request.syscall == sys_readdir)
		{
			char first_buffer[DATA_SIZE];
			char second_buffer[DATA_SIZE];

			results[0] = send_data(request, first_buffer, size, 0);
			results[1] = send_data(request, second_buffer, size, 1);

			// printf("Comparing: \n%s \nwith \n%s\n", first_buffer, second_buffer);

			if (strlen(first_buffer) > strlen(second_buffer))
			{
				strcpy(buffer, first_buffer);
				return results[0];
			}
			else
			{
				strcpy(buffer, second_buffer);
				return results[1];
			}

			break;
		}
		else
			results[i] = send_data(request, buffer, size, i);
	}

	if (request.syscall == sys_open)
	{
		if (results[0] == 0 && results[1] != 0)
		{
			call_restore(request, 0);
			return results[0];
		}
		else if (results[0] != 0 && results[1] == 0)
		{
			call_restore(request, 1);
			return results[1];
		}
	}

	return results[result_index];
}

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "getattr", path);
	struct request request;
	request.syscall = sys_getattr;
	strcpy(request.path, path);

	int status = raid_controller(request, (void *)stbuf, sizeof(struct stat));

	return status;
}

static int net_mkdir(const char *path, mode_t mode)
{
	printf("%s\n", "mkdir");
	struct request request;
	request.syscall = sys_mkdir;
	strcpy(request.path, path);
	request.mode = mode;

	return raid_controller(request, NULL, 0);
}

static int net_unlink(const char *path)
{
	printf("%s\n", "unlink");
	struct request request;
	request.syscall = sys_unlink;
	strcpy(request.path, path);

	return raid_controller(request, NULL, 0);
}

static int net_rmdir(const char *path)
{
	printf("%s\n", "rmdir");
	struct request request;
	request.syscall = sys_rmdir;
	strcpy(request.path, path);

	return raid_controller(request, NULL, 0);
}

static int net_rename(const char *path, const char *new_path)
{
	printf("%s\n", "rename");
	struct request request;
	request.syscall = sys_rename;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return raid_controller(request, NULL, 0);
}

static int net_link(const char *path, const char *new_path)
{
	printf("%s\n", "link");
	struct request request;
	request.syscall = sys_link;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return raid_controller(request, NULL, 0);
}

static int net_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	printf("%s\n", "chmod");

	struct request request;
	request.syscall = sys_chmod;
	strcpy(request.path, path);
	request.mode = mode;
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

static int net_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	printf("%s\n", "truncate");

	struct request request;
	request.syscall = sys_truncate;
	strcpy(request.path, path);
	request.offset = offset;
	if (fi != NULL)
		request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

static int net_open(const char *path, struct fuse_file_info *fi)
{
	printf("%s\n", "open");

	struct request request;
	request.syscall = sys_open;
	strcpy(request.path, path);
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

struct thread_rw_data
{
	struct request request;
	char *read_buffer;
	char *write_buffer;
	int server;
};

static int read_write(void *arg)
{
	struct thread_rw_data data = *(struct thread_rw_data *)arg;

	int status = -errno;
	if (data.read_buffer != NULL)
	{
		write(server_connections[data.server], &data.request, sizeof(data.request));
		struct rw_response response;
		read(server_connections[data.server], &response, sizeof(struct rw_response));
		status = response.status;
		size_t size = response.size;
		read(server_connections[data.server], data.read_buffer, size);
		// printf("Reads: %d + %d = %d\n", sizeof(struct rw_response), size, sizeof(struct rw_response) + size);
	}
	else if (data.write_buffer != NULL)
	{
		MD5_CTX context;
		MD5_Init(&context);
		MD5_Update(&context, data.write_buffer, data.request.size);
		MD5_Final(data.request.digest, &context);

		int i = 0;
		for (i = 0; i < MD5_DIGEST_LENGTH; i++)
			printf("%02x", data.request.digest[i]);
		printf("\n");

		// printf("Hash: %s\n", request.digest);
		write(server_connections[data.server], &data.request, sizeof(data.request));

		// printf("%s\n", write_buffer);
		// printf("len: %d\n", strlen((char *)write_buffer));
		// printf("size: %d\n", request.size);
		write(server_connections[data.server], data.write_buffer, data.request.size);
		struct rw_response response;
		read(server_connections[data.server], &response, sizeof(struct rw_response));
		status = response.status;
	}

	return status;
}

static int rw_raid_controller(struct request request, char *read_buffer, char *write_buffer)
{
	size_t server_count = 2;

	// TODO: Don't read from both of them.
	// HACK: Don't do this man, ffs.
	if (request.syscall == sys_read)
		server_count = 1;

	pthread_t threads[server_count];
	struct thread_rw_data data[server_count];
	int return_values[server_count];

	size_t i;
	for (i = 0; i < server_count; i++)
	{
		data[i].request = request;
		data[i].read_buffer = read_buffer;
		data[i].write_buffer = write_buffer;
		data[i].server = i;

		pthread_create(&threads[i], NULL, &read_write, &data[i]);
		// return_values[i] = read_write(request, read_buffer, write_buffer, i);
	}

	for (i = 0; i < server_count; i++)
		pthread_join(threads[i], (void **)&return_values[i]);

	// printf("SOSOOOOOOOOOO: %d %d\n", return_values[0], return_values[1]);
	return return_values[0];
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

	return rw_raid_controller(request, buffer, NULL);
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	printf("%s\n", "write");
	// printf("%s\n", buffer);
	// printf("len: %d\n", strlen((char *)buffer));
	// printf("size: %d\n", size);

	struct request request;
	request.syscall = sys_write;
	strcpy(request.path, path);
	request.size = size;
	request.offset = offset;
	request.fi = *fi;

	void *cache = malloc(size);
	memcpy(cache, buffer, size);
	int status = rw_raid_controller(request, NULL, cache);

	free(cache);
	return status;
}

static int net_release(const char *path, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "release", path);

	struct request request;
	request.syscall = sys_release;
	strcpy(request.path, path);
	request.fi = *fi;

	raid_controller(request, NULL, 0);

	return 0;
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

	return raid_controller(request, NULL, 0);
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
	int result = raid_controller(request, dirlist, DATA_SIZE);

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
	return 0;
}

static int net_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	printf("%s: %s \n", "create", path);

	struct request request;
	request.syscall = sys_create;
	strcpy(request.path, path);
	request.mode = mode;
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

struct fuse_operations net_oper = {
	.getattr = net_getattr,
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
	.release = net_release,
	.setxattr = net_setxattr,
	.getxattr = net_getxattr,

	// .access = net_access,
	// .utimens = net_utimens,
	.opendir = net_opendir,
	.readdir = net_readdir,
	.releasedir = net_releasedir,
	.create = net_create,
};

int main(int argc, char *argv[])
{
	if (argc <= 1)
	{
		fprintf(stderr, "%s\n", "Please provide a configuration file");
		exit(EXIT_FAILURE);
	}

	struct client_config config;

	vector_new(&storages, sizeof(struct raid_storage), NULL, 2);
	parse_config((char *)argv[1], &storages, &config);

	// fprintf(stdout, "%s\n", config.error_log);
	// fprintf(stdout, "%s\n", config.cache_size);
	// fprintf(stdout, "%s\n", config.cache_replacement);
	// fprintf(stdout, "%d\n", config.timeout);

	struct raid_storage *s = vector_nth(&storages, storage_index);
	// fprintf(stdout, "%s\n", s->disk_name);
	// fprintf(stdout, "%s\n", s->mount_point);
	// fprintf(stdout, "%s\n", s->hot_swap);
	// fprintf(stdout, "%d\n", s->raid);
	// vector_map(&s->servers, &print_fn, NULL);

	int arg = 4;
	char *args[arg + 1];
	args[0] = "./server";
	args[1] = s->mount_point;
	args[2] = "-f";
	args[3] = "-s";
	args[4] = NULL;

	size_t i;
	for (i = 0; i < 2; i++)
	{
		char *server = strdup(vector_nth(&s->servers, i));
		char *ip = strsep(&server, ":");
		int port = atoi(strsep(&server, ":"));

		server_connections[i] = socket(AF_INET, SOCK_STREAM, 0);

		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(port);
		addr.sin_addr.s_addr = inet_addr(ip);

		printf("Connecting: %s:%d\n", ip, port);
		connect(server_connections[i], (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	}

	// struct request r;
	// raid_controller(r, NULL, 0);

	return fuse_main(arg, args, &net_oper, NULL);
}
