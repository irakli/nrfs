// #include <fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include "vector.h"

struct raid_storage
{
	int raid;
	char disk_name[128];
	char mount_point[256];
	char hot_swap[256];
	char *servers[256];
};

vector storages;

void parse_config(const char *config)
{
	FILE *file = fopen(config, "r");
	if (file == NULL)
	{
		fprintf(stderr, "%s\n", "Can't read provided file");
		exit(EXIT_FAILURE);
	}
	char line[256];

	// Parse the first five lines. (for-cycle)

	while (fgets(line, sizeof(line), file))
	{
		/* Remove trailing newline character if it exists. */
		size_t len = strlen(line);
		if (len > 0 && line[len - 1] == '\n')
			line[--len] = '\0';

		if (strncmp(line, "diskname", strlen("diskname")) == 0)
		{
			struct raid_storage *storage = malloc(sizeof(struct raid_storage));
			strncpy(storage->disk_name, line + strlen("diskname = "), strlen(line) - strlen("diskname = "));
			vector_append(&storages, storage);
			// printf("%s", storage->disk_name);
		}

		if (strncmp(line, "mountpoint", strlen("mountpoint")) == 0)
		{
			struct raid_storage *storage = vector_last(&storages);
			strncpy(storage->mount_point, line + strlen("mountpoint = "), strlen(line) - strlen("mountpoint = "));
		}

		if (strncmp(line, "hotswap", strlen("hotswap")) == 0)
		{
			struct raid_storage *storage = vector_last(&storages);
			strncpy(storage->hot_swap, line + strlen("hotswap = "), strlen(line) - strlen("hotswap = "));
		}

		if (strncmp(line, "raid", strlen("raid")) == 0)
		{
			struct raid_storage *storage = vector_last(&storages);
			storage->raid = atoi(line + strlen("raid = "));
		}

		// Servers
	}

	fclose(file);
}

int main(int argc, char const *argv[])
{
	if (argc <= 1)
	{
		fprintf(stderr, "%s\n", "Please provide a configuration file");
		exit(EXIT_FAILURE);
	}

	vector_new(&storages, sizeof(struct raid_storage), NULL, 2);

	parse_config((char *)argv[1]);

	struct raid_storage *s = vector_last(&storages);
	printf("%s\n", s->disk_name);
	printf("%s\n", s->mount_point);
	printf("%s\n", s->hot_swap);
	printf("%d\n", s->raid);

	return 0;
}
