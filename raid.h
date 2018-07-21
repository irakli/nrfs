#define FUSE_USE_VERSION 26

#include <fuse.h>

#define MAX_NAME_LENGTH 128
#define MAX_PATH_LENGTH 256
#define MAX_IP_LENGTH 24 // 255.255.255.255:65535 (22)

enum syscalls
{
	sys_getattr,
	sys_mknod,
	sys_mkdir,
	sys_unlink,
	sys_rmdir,
	sys_rename,
	sys_link,
	sys_chmod,
	sys_truncate,
	sys_open,
	sys_read,
	sys_write,
	sys_statfs,
	sys_flush,
	sys_release,
	sys_setxattr,
	sys_getxattr,
	sys_opendir,
	sys_readdir,
	sys_releasedir,
	sys_create,
	sys_init
};

struct request
{
	enum syscalls syscall;
	char path[MAX_PATH_LENGTH];
	char new_path[MAX_PATH_LENGTH];
	mode_t mode;
	dev_t dev;
	off_t offset;
	size_t size;

	struct fuse_file_info fi;
};

// gcc -Wall raid_server.c vector.c `pkg-config fuse --cflags --libs` -o server.out -lpthread
// gcc -Wall raid_client.c vector.c `pkg-config fuse --cflags --libs` -o client.out