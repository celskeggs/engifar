#ifndef MAPS_PARSE_H

#include <stdbool.h>
#include <stdint.h>

struct maps_entry {
	uint64_t from, to;
	bool read, write, execute;
	uint64_t offset;
};

struct maps_structure {
	int count;
	struct maps_entry *entries;
};

#endif
