
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>

struct chunk {
	uint64_t size;
	uint8_t *ptr;
};
struct map {
	struct chunk chunk;
	int fd;
};

struct map *mmap_file(char *name) {
	struct map *out = malloc(sizeof(struct map));
	if (out == NULL) {
		return NULL;
	}
	int fd = open(name, O_RDONLY);
	if (fd == -1) {
		return NULL;
	}
	struct stat status;
	if (fstat(fd, &status) == -1) {
		int l_errno = errno;
		close(fd);
		errno = l_errno;
		return NULL;
	}
	out->chunk.size = (uint64_t) status.st_size;
	void *block = mmap(NULL, status.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (block == MAP_FAILED) {
		int l_errno = errno;
		close(fd);
		return NULL;
	}
	out->chunk.ptr = block;
	out->fd = fd;
	return out;
}

void mmap_close(struct map *m) {
	if (munmap(m->chunk.ptr, m->chunk.size) == -1) {
		perror("could not unmap");
	}
	if (close(m->fd) == -1) {
		perror("could not close fd");
	}
}

uint64_t trim_ends(struct chunk *A, struct chunk *B) {
	int si = 0;
	while (si < A->size && si < B->size && A->ptr[si] == B->ptr[si]) {
		si++;
	}
#ifdef ALIGN_BEGIN
	si &= ~15;
#endif
	A->ptr += si;
	A->size -= si;
	B->ptr += si;
	B->size -= si;
	int ei = 0;
	while (A->size - ei > 0 && B->size - ei > 0 && A->ptr[A->size - 1 - ei] == B->ptr[B->size - 1 - ei]) {
		ei++;
	}
	A->size -= ei;
	B->size -= ei;
	return si;
}

void print_row(int32_t *array, int len) {
	int i;
	putchar('>');
	for (i=0; i<len; i++) {
		printf(" %d", array[i]);
	}
	putchar('\n');
}

int main(int argc, char *argv[]) {
	if (argc != 3) {
		fprintf(stderr, "Usage: %s <FROM> <TO>\n", argv[0]);
		return 1;
	}

	// > "Fetch" data
	struct map *Af = mmap_file(argv[1]);
	if (Af == NULL) {
		perror(argv[1]);
		return 1;
	}
	struct map *Bf = mmap_file(argv[2]);
	if (Bf == NULL) {
		perror(argv[2]);
		return 1;
	}

	// > Minimize the chunk that we're analyzing.
	struct chunk Ac = Af->chunk;
	struct chunk Bc = Bf->chunk;
	uint64_t start_i = trim_ends(&Ac, &Bc);
	printf("Files are %.6x/%.6x, changed in %.6x-%.6x/%.6x-%.6x\n", Af->chunk.size, Bf->chunk.size, start_i, Ac.size - 1 + start_i, start_i, Bc.size - 1 + start_i);

	if (Ac.size <= 0 || Bc.size <= 0) {
		fputs("At least one empty input - files possibly equal. Erroneous output possible.\n", stderr);
	}

	// > Build table

	// X is A, Y is B.
	uint32_t width = Ac.size + 1;
	uint32_t height = Bc.size + 1;

	if (width < Ac.size || height < Bc.size) {
		fputs("Overflow - files too large.\n", stderr);
		return 1;
	}

	uint8_t *sources = calloc(width * height, sizeof(uint8_t));
#define S_LEFT 0
#define S_TOP 1
#define S_DIAG_MOD 2
#define S_DIAG_UNMOD 3
	uint32_t x, y;
	for (y=1; y<height; y++) {
		sources[y * width] = S_TOP;
	}
	for (x=1; x<width; x++) {
		sources[x] = S_LEFT;
	}
	printf("Size: %dx%d\n", width, height);
	int32_t *inactive_row = malloc(width * sizeof(int32_t));
	int32_t *active_row = malloc(width * sizeof(int32_t));
	for (x=0; x<width; x++) {
		inactive_row[x] = -x;
	}
	//print_row(inactive_row, width);
	for (y=1; y<height; y++) {
		uint64_t ytw = y * width;
		uint8_t bd_y = Bc.ptr[y - 1];
		int32_t best = -y;
		active_row[0] = best;
		for (x=1; x<width; x++) {
			int32_t leftval = best; // -17
			int32_t topval = inactive_row[x]; // -1
			bool match = (bd_y == Ac.ptr[x - 1]); // 0
			int32_t diagval = inactive_row[x - 1] + (match ? 2 : 0); // can this branch be removed? // -19
			if (leftval <= diagval && topval <= diagval) {
				sources[x + ytw] = match ? S_DIAG_UNMOD : S_DIAG_MOD; // 1
				best = diagval; // -1
			} else if (leftval <= topval) {
				sources[x + ytw] = S_TOP;
				best = topval;
			} else {
				sources[x + ytw] = S_LEFT;
				best = leftval;
			}
			active_row[x] = (--best);
		}
		//print_row(active_row, width);
		int32_t *temp = inactive_row;
		inactive_row = active_row;
		active_row = temp;
		if (height > 1000 && (y % (uint32_t) (height / 100)) == 0) {
			printf("\r%.2d%% done.", 100 * ((uint64_t) y) / height);
		}
	}

	// > Build trace

	puts("Trace:");
	uint8_t *trace_move = malloc(width + height + 1);
	uint8_t *trace_move_end = trace_move + (width + height + 1);
	uint8_t *trace_move_active = trace_move_end;
	x = width - 1;
	y = height - 1;
	while (1) {
		if (x == 0 && y == 0) {
			break;
		}
		uint8_t source = sources[x + y * width];
		*(--trace_move_active) = source;
		switch (source) {
		case S_LEFT: x--; break;
		case S_TOP: y--; break;
		case S_DIAG_MOD:
		case S_DIAG_UNMOD: x--; y--; break;
		default: fputs("Internal error: bad source.\n", stderr); exit(1);
		}
	}

	// > Follow trace
	x = y = 0;
#define LINE_LENGTH 16
// ELEM_NONE is an arbitrary constant less than 256.
#define MAKE_ELEM(c,w) ((c) + ((w) << 8))
#define ELEM_NONE MAKE_ELEM(202, '\0')
#define ELEM_DONE MAKE_ELEM(204, '\0')
#define ELEM_UNMOD(x) MAKE_ELEM(x, ' ')
#define ELEM_MOD(x) MAKE_ELEM(x, '=')
#define ELEM_INS(x) MAKE_ELEM(x, '+')
#define ELEM_DEL(x) MAKE_ELEM(x, '-')
	uint16_t line_a[LINE_LENGTH];
	uint16_t line_b[LINE_LENGTH];
	uint64_t off_a, off_b;
	uint8_t ln_i = 0;
	bool any_mods = false, last_unmodded = false;
	while (trace_move_active < trace_move_end) {
		uint8_t cur_trace = *(trace_move_active++);
		if (ln_i == 0) {
			off_a = x;
			off_b = y;
		}
		switch (cur_trace) {
		case S_LEFT:
			line_a[ln_i] = ELEM_DEL(Ac.ptr[x++]);
			line_b[ln_i] = ELEM_NONE;
			any_mods = true;
			break;
		case S_TOP:
			line_a[ln_i] = ELEM_NONE;
			line_b[ln_i] = ELEM_INS(Bc.ptr[y++]);
			any_mods = true;
			break;
		case S_DIAG_UNMOD:
			line_a[ln_i] = ELEM_UNMOD(Ac.ptr[x++]);
			line_b[ln_i] = ELEM_UNMOD(Bc.ptr[y++]);
			break;
		case S_DIAG_MOD:
			line_a[ln_i] = ELEM_MOD(Ac.ptr[x++]);
			line_b[ln_i] = ELEM_MOD(Bc.ptr[y++]);
			any_mods = true;
			break;
		}
		if (++ln_i >= LINE_LENGTH || trace_move_active >= trace_move_end) { // END OF LINE - PRINT IT!
			uint8_t loc_i;
			if (!any_mods) {
				if (!last_unmodded) {
					puts("...");
				}
				/*printf("0x%.6x: ...  ", off_a + start_i);
				for (loc_i=1; loc_i<LINE_LENGTH; loc_i++) {
					fputs("     ", stdout);
				}
				printf("0x%.6x: ...", off_b + start_i);*/
				last_unmodded = true;
			} else {
				// If we didn't get a full line (end of the analyzed section), then add in the next bytes from the source data.
				for (loc_i=ln_i; loc_i < LINE_LENGTH; loc_i++) {
					if (loc_i - ln_i + start_i + width - 1 >= Af->chunk.size) {
						line_a[loc_i] = ELEM_DONE;
					} else {
						line_a[loc_i] = ELEM_UNMOD(Af->chunk.ptr[loc_i - ln_i + start_i + width - 1]);
					}
					if (loc_i - ln_i + start_i + height - 1 >= Bf->chunk.size) {
						line_b[loc_i] = ELEM_DONE;
					} else {
						line_b[loc_i] = ELEM_UNMOD(Bf->chunk.ptr[loc_i - ln_i + start_i + height - 1]);
					}
				}
				// Format the output.
				printf("0x%.6x: ", off_a + start_i);
				for (loc_i=0; loc_i<LINE_LENGTH; loc_i++) {
					uint16_t elem = line_a[loc_i];
					if (elem == ELEM_NONE) {
						fputs(" --  ", stdout);
					} else if (elem == ELEM_DONE) {
						fputs(" ..  ", stdout);
					} else {
						printf("%c%.2x%c ", elem >> 8, elem & 0xFF, elem >> 8);
					}
				}
				printf("0x%.6x: ", off_b + start_i);
				for (loc_i=0; loc_i<LINE_LENGTH; loc_i++) {
					uint16_t elem = line_b[loc_i];
					if (elem == ELEM_NONE) {
						fputs(" --  ", stdout);
					} else {
						printf("%c%.2x%c ", elem >> 8, elem & 0xFF, elem >> 8);
					}
				}
				printf(" [");
				for (loc_i=0; loc_i<LINE_LENGTH; loc_i++) {
					uint8_t elem = line_a[loc_i] & 0xFF;
					printf("%c", (elem >= 32 && elem <= 126) ? elem : '.');
				}
				printf("] [");
				for (loc_i=0; loc_i<LINE_LENGTH; loc_i++) {
					uint8_t elem = line_b[loc_i] & 0xFF;
					printf("%c", (elem >= 32 && elem <= 126) ? elem : '.');
				}
				printf("]\n");
				last_unmodded = false;
			}
			ln_i = 0;
			any_mods = false;
		}
	}

	mmap_close(Af);
	mmap_close(Bf);
	return 1;
}

