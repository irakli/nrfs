// #include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "vector.h"

struct raid_server
{
	int raid;
	char *disk_name;
	char *mount_point;
	char *hot_swap;
	char **servers;
};

void parse_config(const char *config)
{
	FILE *file = fopen(config, "r");
	if (file == NULL)
	{
		fprintf(stderr, "%s\n", "Can't read provided file");
		exit(EXIT_FAILURE);
	}
	char line[256];

	while (fgets(line, sizeof(line), file))
	{
		fprintf(stdout, "%s", line);
	}

	fprintf(stdout, "\n");
	fclose(file);
}

int main(int argc, char const *argv[])
{
	if (argc <= 1)
	{
		fprintf(stderr, "%s\n", "Please provide a configuration file");
		exit(EXIT_FAILURE);
	}

	parse_config((char *)argv[1]);

	vector example;
	vector_new(&example, sizeof(int), NULL, 4);
	int a = 31232;
	vector_append(&example, &a);
	printf("%d\n", *(int *)vector_nth(&example, 0));

	return 0;
}
