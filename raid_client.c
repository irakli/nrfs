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
#include <time.h>
#include <sys/stat.h>
#include <fcntl.h>
#include "vector.h"

struct client_config_t
{
	char error_log[MAX_PATH_LENGTH];
	char cache_size[16];
	char cache_replacement[16];
	int timeout;
};

struct raid_storage_t
{
	int raid;
	char disk_name[MAX_NAME_LENGTH];
	char mount_point[MAX_PATH_LENGTH];
	char hot_swap[MAX_IP_LENGTH];
	vector servers;
};

struct server_t
{
	char *ip;
	int port;
};

static vector storages;
struct client_config_t config;
static int storage_index;
static int server_connections[2];
static struct server_t servers[3];
static struct raid_storage_t *storage;
static int main_server;
static pthread_mutex_t mutex;
static pthread_t swap_thread;
static int swapped;
static int log_file;

static void parse_config(const char *config_file, vector *storages)
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
	strncpy(config.error_log, line + strlen("errorlog = "), strlen(line) - strlen("errorlog = ") - 1);

	/* Cache size. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	strncpy(config.cache_size, line + strlen("cache_size = "), strlen(line) - strlen("cache_size = ") + 1);

	/* Cache replacement algorithm. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	strncpy(config.cache_replacement, line + strlen("cache_replacment = "), strlen(line) - strlen("cache_replacment = ") + 1);

	/* Timeout. */
	fgets(line, sizeof(line), file);
	line[strlen(line) - 1] = '\0';
	config.timeout = atoi(line + strlen("timeout = "));

	while (fgets(line, sizeof(line), file))
	{
		/* Remove trailing newline character if it exists. */
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[--len] = '\0';

		/* Disk name. */
		if (strncmp(line, "diskname", strlen("diskname")) == 0)
		{
			struct raid_storage_t *storage = malloc(sizeof(struct raid_storage_t));
			vector_new(&storage->servers, MAX_IP_LENGTH, NULL, 2);

			strncpy(storage->disk_name, line + strlen("diskname = "), strlen(line) - strlen("diskname = ") - 1);
			vector_append(storages, storage);
		}

		/* Mount point. */
		if (strncmp(line, "mountpoint", strlen("mountpoint")) == 0)
		{
			struct raid_storage_t *storage = vector_last(storages);
			strncpy(storage->mount_point, line + strlen("mountpoint = "), strlen(line) - strlen("mountpoint = ") - 1);
		}

		/* Hotswap. */
		if (strncmp(line, "hotswap", strlen("hotswap")) == 0)
		{
			struct raid_storage_t *storage = vector_last(storages);
			strncpy(storage->hot_swap, line + strlen("hotswap = "), strlen(line) - strlen("hotswap = "));
		}

		/* RAID. */
		if (strncmp(line, "raid", strlen("raid")) == 0)
		{
			struct raid_storage_t *storage = vector_last(storages);
			storage->raid = atoi(line + strlen("raid = "));
		}

		/* Server list. */
		if (strncmp(line, "servers", strlen("servers")) == 0)
		{
			struct raid_storage_t *storage = vector_last(storages);
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

static char *get_time()
{

	time_t current_time;
	char *date_time;

	/* Obtain current time. */
	current_time = time(NULL);

	/* Convert to local time format. */
	date_time = ctime(&current_time);

	/* Print to stdout. ctime() has already added a terminating newline character. */
	date_time[strlen(date_time) - 1] = '\0';

	return date_time;
}

int reconnecting = 0;
static int swap_server(int index)
{
	swapped = 1;

	struct server_t temp = servers[index];
	servers[index] = servers[2];
	servers[2] = temp;

	server_connections[index] = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(servers[index].port);
	addr.sin_addr.s_addr = inet_addr(servers[index].ip);
	connect(server_connections[index], (struct sockaddr *)&addr, sizeof(struct sockaddr_in));

	struct request_t request;
	request.syscall = sys_swap_send;
	strcpy(request.ip, servers[index].ip);
	request.port = servers[index].port;

	write(server_connections[!index], &request, sizeof(struct request_t));

	int result;
	read(server_connections[!index], &result, sizeof(result));

	dprintf(log_file, "[%s] %s %s:%d hot swap server added\n", get_time(), storage->disk_name, servers[index].ip, servers[index].port);
	reconnecting = 0;
}

static void *try_connect(void *arg)
{
	int index = *(int *)arg;

	// Swap main server.
	main_server = !main_server;

	int status;
	int sfd = socket(AF_INET, SOCK_STREAM, 0);
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(servers[index].port);
	addr.sin_addr.s_addr = inet_addr(servers[index].ip);

	time_t endwait;
	time_t start = time(NULL);
	time_t seconds = config.timeout; // end loop after this time has elapsed

	endwait = start + seconds;

	while (start < endwait)
	{
		/* Do stuff while waiting */
		status = connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
		if (status != -1)
		{
			main_server = !main_server;
			// printf("Reconnected!!! Main server is now: %d\n", main_server);
			// printf("fd: %d", log_file);
			dprintf(log_file, "[%s] %s %s:%d reconnected\n", get_time(), storage->disk_name, servers[index].ip, servers[index].port);
			server_connections[index] = sfd;
			reconnecting = 0;
			return;
		}

		sleep(1); // sleep 1s.
		start = time(NULL);
	}

	// hotswap
	if (!swapped)
		swap_server(index);
	else
		reconnecting = 0;
}

static void reconnect(int index)
{
	if (reconnecting)
		return;
	dprintf(log_file, "[%s] %s %s:%d server declared as lost\n", get_time(), storage->disk_name, servers[index].ip, servers[index].port);
	reconnecting = 1;
	pthread_create(&swap_thread, NULL, &try_connect, &index);
}

static int send_data(struct request_t request, void *buffer, size_t size, int server)
{
	// printf("Connecting to: server%d\n", server);
	int s = write(server_connections[server], &request, sizeof(request));
	if (s <= 0)
	{
		// printf("%d\n", s);
		// printf("No connection to the server: %d\n", server);
		return -no_connection;
	}

	int status = -errno;
	if (buffer != NULL)
	{
		struct response_t response;
		read(server_connections[server], &response, sizeof(struct response_t));
		status = response.status;
		memcpy(buffer, response.data, size);
	}
	else
		read(server_connections[server], &status, sizeof(status));

	return status;
}

static void call_restore(struct request_t request, int index)
{
	// Copy index -> !index
	struct request_t send_request;
	send_request.syscall = sys_restore_send;
	strcpy(send_request.path, request.path);
	strcpy(send_request.ip, servers[!index].ip);
	send_request.port = servers[!index].port;

	write(server_connections[index], &send_request, sizeof(struct request_t));
}

static int raid_controller(struct request_t request, void *buffer, size_t size)
{
	size_t server_count = 2;

	int results[server_count];
	int result_index = 0;

	size_t i;
	for (i = 0; i < server_count; i++)
	{
		if (request.syscall == sys_getattr)
		{
			results[main_server] = send_data(request, buffer, size, main_server);
			if (results[main_server] == 0)
				return 0;
			else
			{
				results[main_server] = send_data(request, buffer, size, !main_server);
				return results[main_server];
			}
		}
		else if (request.syscall == sys_readdir)
		{
			char first_buffer[DATA_SIZE];
			char second_buffer[DATA_SIZE];

			results[0] = send_data(request, first_buffer, size, 0);
			results[1] = send_data(request, second_buffer, size, 1);

			// printf("Comparing: \n%s \nwith \n%s\n", first_buffer, second_buffer);

			if (results[0] != no_connection && strlen(first_buffer) > strlen(second_buffer))
			{
				strcpy(buffer, first_buffer);
				return results[0];
			}
			else if (results[1] != no_connection && strlen(first_buffer) < strlen(second_buffer))
			{
				strcpy(buffer, second_buffer);
				return results[1];
			}
			else
				strcpy(buffer, first_buffer);

			break;
		}
		else
			results[i] = send_data(request, buffer, size, i);
	}

	for (i = 0; i < server_count; i++)
		if (results[i] == -no_connection)
		{
			reconnect(i);
			main_server = !i;
			results[i] == results[!i];
		}

	if (request.syscall == sys_open)
	{
		if (results[main_server] == 0 && results[!main_server] != 0 && results[!main_server] != -no_connection)
		{
			call_restore(request, main_server);
			dprintf(log_file, "[%s] %s restoring %s on %s:%d from %s:%d\n", get_time(), storage->disk_name, request.path, servers[main_server].ip, servers[main_server].port, servers[!main_server].ip, servers[!main_server].port);
			return results[main_server];
		}
		else if (results[!main_server] == 0 && results[main_server] != 0 && results[main_server] != -no_connection)
		{
			call_restore(request, !main_server);
			dprintf(log_file, "[%s] %s restoring %s on %s:%d from %s:%d\n", get_time(), storage->disk_name, request.path, servers[!main_server].ip, servers[!main_server].port, servers[main_server].ip, servers[main_server].port);
			return results[!main_server];
		}
	}

	// printf("%d %d %d\n\n", results[0], results[1], main_server);

	return results[main_server];
}

static int net_getattr(const char *path, struct stat *stbuf, struct fuse_file_info *fi)
{
	struct request_t request;
	request.syscall = sys_getattr;
	strcpy(request.path, path);

	int status = raid_controller(request, (void *)stbuf, sizeof(struct stat));

	return status;
}

static int net_mkdir(const char *path, mode_t mode)
{
	struct request_t request;
	request.syscall = sys_mkdir;
	strcpy(request.path, path);
	request.mode = mode;

	return raid_controller(request, NULL, 0);
}

static int net_unlink(const char *path)
{
	struct request_t request;
	request.syscall = sys_unlink;
	strcpy(request.path, path);

	return raid_controller(request, NULL, 0);
}

static int net_rmdir(const char *path)
{
	struct request_t request;
	request.syscall = sys_rmdir;
	strcpy(request.path, path);

	return raid_controller(request, NULL, 0);
}

static int net_rename(const char *path, const char *new_path)
{
	struct request_t request;
	request.syscall = sys_rename;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return raid_controller(request, NULL, 0);
}

static int net_link(const char *path, const char *new_path)
{
	struct request_t request;
	request.syscall = sys_link;
	strcpy(request.path, path);
	strcpy(request.new_path, new_path);

	return raid_controller(request, NULL, 0);
}

static int net_chmod(const char *path, mode_t mode, struct fuse_file_info *fi)
{
	struct request_t request;
	request.syscall = sys_chmod;
	strcpy(request.path, path);
	request.mode = mode;
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

static int net_truncate(const char *path, off_t offset, struct fuse_file_info *fi)
{
	struct request_t request;
	request.syscall = sys_truncate;
	strcpy(request.path, path);
	request.offset = offset;
	if (fi != NULL)
		request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

static int net_open(const char *path, struct fuse_file_info *fi)
{
	struct request_t request;
	request.syscall = sys_open;
	strcpy(request.path, path);
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

struct thread_rw_data
{
	struct request_t request;
	char *read_buffer;
	char *write_buffer;
	char *ip;
	int port;
};

static int read_write(void *arg)
{
	struct thread_rw_data data = *(struct thread_rw_data *)arg;

	int sfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(data.port);
	addr.sin_addr.s_addr = inet_addr(data.ip);

	int server_status = connect(sfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	if (server_status == -1)
	{
		close(sfd);
		return -no_connection;
	}

	int status = -errno;
	if (data.request.syscall == sys_read)
	{
		write(sfd, &data.request, sizeof(data.request));
		struct rw_response_t response;
		read(sfd, &response, sizeof(struct rw_response_t));
		status = response.status;
		size_t size = response.size;
		read(sfd, data.read_buffer, size);
		// printf("Reads: %d + %d = %d\n", sizeof(struct rw_response_t), size, sizeof(struct rw_response_t) + size);
	}
	else if (data.request.syscall == sys_write)
	{
		MD5_CTX context;
		MD5_Init(&context);
		MD5_Update(&context, data.write_buffer, data.request.size);
		MD5_Final(data.request.digest, &context);

		int i = 0;
		for (i = 0; i < MD5_DIGEST_LENGTH; i++)
			printf("%02x", data.request.digest[i]);
		printf("\n");

		write(sfd, &data.request, sizeof(data.request));
		write(sfd, data.write_buffer, data.request.size);
		struct rw_response_t response;
		read(sfd, &response, sizeof(struct rw_response_t));
		status = response.status;
	}

	close(sfd);
	return status;
}

static int rw_raid_controller(struct request_t request, char *read_buffer, char *write_buffer)
{
	size_t server_count = 2;
	pthread_t threads[server_count];
	struct thread_rw_data data[server_count];
	int results[server_count];

	size_t i;
	if (request.syscall == sys_read)
	{
		for (i = 0; i < server_count; i++)
		{
			// printf("Reading from: server %d\n", main_server);
			data[main_server].request = request;
			data[main_server].read_buffer = read_buffer;
			data[main_server].write_buffer = write_buffer;
			data[main_server].ip = servers[main_server].ip;
			data[main_server].port = servers[main_server].port;

			results[main_server] = read_write(&data[main_server]);

			if (results[main_server] != -no_connection)
				break;
			else
				reconnect(main_server);
		}
	}
	else
	{
		for (i = 0; i < server_count; i++)
		{
			data[i].request = request;
			data[i].read_buffer = read_buffer;
			data[i].write_buffer = write_buffer;
			data[i].ip = servers[i].ip;
			data[i].port = servers[i].port;

			pthread_create(&threads[i], NULL, &read_write, &data[i]);
		}

		for (i = 0; i < server_count; i++)
			pthread_join(threads[i], (void **)&results[i]);
	}

	return results[main_server];
}

static int net_read(const char *path, char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct request_t request;
	request.syscall = sys_read;
	strcpy(request.path, path);
	request.size = size;
	request.offset = offset;
	request.fi = *fi;

	return rw_raid_controller(request, buffer, NULL);
}

static int net_write(const char *path, const char *buffer, size_t size, off_t offset, struct fuse_file_info *fi)
{
	struct request_t request;
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
	struct request_t request;
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
	struct request_t request;
	request.syscall = sys_opendir;
	strcpy(request.path, path);
	request.fi = *fi;

	return raid_controller(request, NULL, 0);
}

static int net_readdir(const char *path, void *buffer, fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	struct request_t request;
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
	struct request_t request;
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

	vector_new(&storages, sizeof(struct raid_storage_t), NULL, 2);
	parse_config((char *)argv[1], &storages);

	storage = vector_nth(&storages, storage_index);

	int arg = 4;
	char *args[arg + 1];
	args[0] = "./server";
	args[1] = storage->mount_point;
	args[2] = "-f";
	args[3] = "-s";
	args[4] = NULL;

	size_t i;
	for (i = 0; i < 2; i++)
	{
		char *server_string = strdup(vector_nth(&storage->servers, i));
		servers[i].ip = strdup(strsep(&server_string, ":"));
		servers[i].port = atoi(strsep(&server_string, ":"));
	}

	char *hs = malloc(MAX_IP_LENGTH);
	strcpy(hs, storage->hot_swap);
	servers[2].ip = strdup(strsep(&hs, ":"));
	servers[2].port = atoi(strsep(&hs, ":"));
	free(hs);

	log_file = open(config.error_log, O_WRONLY | O_CREAT | O_TRUNC, 0644);

	for (i = 0; i < 2; i++)
	{
		server_connections[i] = socket(AF_INET, SOCK_STREAM, 0);

		struct sockaddr_in addr;
		addr.sin_family = AF_INET;
		addr.sin_port = htons(servers[i].port);
		addr.sin_addr.s_addr = inet_addr(servers[i].ip);

		dprintf(log_file, "[%s] %s:%d open connection\n", get_time(), servers[i].ip, servers[i].port);
		connect(server_connections[i], (struct sockaddr *)&addr, sizeof(struct sockaddr_in));
	}

	main_server = 0;
	swapped = 0;
	pthread_mutex_init(&mutex, NULL);

	return fuse_main(arg, args, &net_oper, NULL);
}
