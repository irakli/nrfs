#define FUSE_USE_VERSION 26

#include <fuse.h>

#define MAX_NAME_LENGTH 128
#define MAX_PATH_LENGTH 256
#define MAX_IP_LENGTH 24 // 255.255.255.255:65535 (22)

struct request
{
	char syscall[MAX_NAME_LENGTH];
	char path[MAX_PATH_LENGTH];
	char new_path[MAX_PATH_LENGTH];
	mode_t mode;
	off_t offset;
	size_t size;
};