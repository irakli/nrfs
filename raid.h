#define FUSE_USE_VERSION 31
#include <fuse.h>
#include <openssl/md5.h>

#define MAX_NAME_LENGTH 128
#define MAX_PATH_LENGTH 256
#define MAX_IP_LENGTH 24 // 255.255.255.255:65535 (22)
#define DATA_SIZE 4096

#define hash_mismatch 1337
#define hash_xattr "user.hash"
#define no_connection 420

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
	sys_access,
	sys_utimens,
	sys_opendir,
	sys_readdir,
	sys_releasedir,
	sys_create,
	sys_init,
	sys_restore_send,
	sys_restore_receive,
	sys_swap_send,
	sys_swap_receive
};

struct __attribute__((__packed__)) request_t
{
	enum syscalls syscall;
	char path[MAX_PATH_LENGTH];
	char new_path[MAX_PATH_LENGTH];
	int mask;
	mode_t mode;
	dev_t dev;
	off_t offset;
	size_t size;

	unsigned char digest[MD5_DIGEST_LENGTH];
	struct fuse_file_info fi;

	char ip[MAX_IP_LENGTH];
	int port;
};

struct __attribute__((__packed__)) response_t
{
	int status;
	char data[DATA_SIZE];

	char actual_hash[MD5_DIGEST_LENGTH * 2];
};

struct __attribute__((__packed__)) rw_response_t
{
	int status;
	size_t size;
};

// gcc -Wall raid_server.c vector.c `pkg-config fuse --cflags --libs` -o server.out -lpthread
// gcc -Wall raid_client.c vector.c `pkg-config fuse --cflags --libs` -o client.out
// fusermount -uz ~/code/final/filesystem/mount

// ./server.out 127.0.0.1 10001 server1/
// ./server.out 127.0.0.1 10002 server2/
// ./client.out config.cfg
