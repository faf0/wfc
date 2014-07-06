/*
 * wfc.cpp
 *
 *      Author: Fabian Foerg
 */

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <map>

#define DEFAULT_NUMBER_CHILDS 4
#define DEFAULT_INPUT_FILE "test_in.txt"
#define DEFAULT_OUTPUT_FILE "test_out.txt"
#define MAX_WORD_LENGTH 64

/**
 * Pair of word and its frequency.
 */
typedef struct word_count_t {
	int count;
	const char *word;
} word_count;

/**
 * Comparator function for map from strings.
 */
static bool cmp_str(const char *lhs, const char *rhs) {
	return (strcmp(lhs, rhs) < 0);
}

/**
 * Comparison method for two word_count objects.
 * Returns an integer less than, equal to, or greater to zero,
 * if the first argument count is lexicographically smaller than,
 * equal to, or less than the second argument count.
 */
static int cmp_int_desc(const void *p1, const void *p2) {
	word_count *wc1 = (word_count *) p1;
	word_count *wc2 = (word_count *) p2;

	return wc2->count - wc1->count;
}

/**
 * Returns zero, iff the given character is skippable, i.e. the given
 * character does not belong to a word.
 */
static int isskip(char c) {
	return !((c >= 'a') && (c <= 'z')) && !((c >= 'A') && (c <= 'Z'))
			&& (c != '-') && (c != '\'');
}

/**
 * Returns the index of the next skippable (if skip is not zero) or word character
 * (if skip is zero) in the buffer, beginning at buffer offset 'offset'.
 * If no such character is found, buffer_length, will be returned.
 */
static long int seek_next(char *buffer, size_t offset, size_t buffer_length,
		int skip) {
	size_t i;

	for (i = offset; i < buffer_length; i++) {
		int skip_char = isskip(buffer[i]);

		if ((skip && skip_char) || (!skip && !skip_char)) {
			break;
		}
	}

	return i;
}

/**
 * Returns the index of the next word character in the buffer, beginning at buffer
 * offset 'offset'.
 * If no such character is found, buffer_length, will be returned.
 */
static long int seek_next_nonskip(char *buffer, size_t offset,
		size_t buffer_length) {
	return seek_next(buffer, offset, buffer_length, 0);
}

/**
 * Returns the index of the next skippable character in the buffer, beginning at
 * buffer offset 'offset'.
 * If no such character is found, buffer_length, will be returned.
 */
static long int seek_next_skip(char *buffer, size_t offset,
		size_t buffer_length) {
	return seek_next(buffer, offset, buffer_length, 1);
}

/**
 * Prunes the resources which were dynamically allocated by the parent
 * process.
 */
static void prune_parent_mem(void *shm, int shmid,
		char **child_word_buffer_offsets, size_t **child_number_words_offsets) {
	if (shm) {
		shmdt(shm);
		shmctl(shmid, IPC_RMID, NULL);
	}
	if (child_word_buffer_offsets) {
		free(child_word_buffer_offsets);
	}
	if (child_number_words_offsets) {
		free(child_number_words_offsets);
	}
}

/**
 * Prunes the resources which were dynamically allocated by a child
 * process.
 */
static void prune_child_mem(FILE *inputfd, char *buffer) {
	if (inputfd) {
		fclose(inputfd);
	}
	if (buffer) {
		free(buffer);
	}
}

/**
 * The child process parses the input file and writes each word followed
 * by a null byte into shared memory, starting at word_buffer.
 * It also saves the number of words written at number_words.
 * It looks for the first complete word in the file, beginning at file_offset.
 * Words which do not begin at file_offset are ignored.
 * Words that start before or at end - 1 are parsed and written into shared memory.
 */
static int child_parse(const char * inputfname, size_t file_offset, size_t end,
		char *word_buffer, size_t *number_words) {
	/*
	 * Leave space for characters between file_offset (inclusive) and end (exclusive).
	 * Additionally, leave space for previous byte (the byte before file_offset) and
	 * last word.
	 * Note that space for skip character is available, if last word starts before or
	 * at end - 1, as specified.
	 */
	const size_t buffer_size = end - file_offset + MAX_WORD_LENGTH + 1;
	char *buffer = NULL;
	size_t buffer_end = 0;
	size_t parse_position = 0;
	size_t parse_bound = end - file_offset;
	char *shm_offset = word_buffer;
	size_t word_counter = 0;
	FILE *inputfd = NULL;

	inputfd = fopen(inputfname, "r");
	if (!inputfd) {
		fprintf(stderr, "Could not open input file!\n");
		exit(EXIT_FAILURE);
	}

	buffer = (char *) malloc(sizeof(char) * buffer_size);
	if (!buffer) {
		fprintf(stderr, "Not enough memory!\n");
		prune_child_mem(inputfd, buffer);
		return EXIT_FAILURE;
	}

	if (file_offset > 0) {
		fseek(inputfd, file_offset - 1, SEEK_SET);
	} else {
		fseek(inputfd, 0, SEEK_SET);
	}

	// fill the buffer with file content
	if ((buffer_end = fread(buffer, sizeof(char), buffer_size, inputfd)) < 0) {
		prune_child_mem(inputfd, buffer);
		return EXIT_FAILURE;
	}

	// prepare parsing of words in buffer
	if (file_offset > 0) {
		// take a look at the previous character

		if (isskip(buffer[0])) {
			// seek for the next word
			parse_position = seek_next_nonskip(buffer, 1, buffer_end);
		} else {
			// previous character belongs to a word

			if ((end - file_offset) > 1) {
				// we have at least one character to parse
				if (isskip(buffer[1])) {
					// seek for the next word
					parse_position = seek_next_nonskip(buffer, 2, buffer_end);
				} else {
					// skip current word and proceed with next one
					parse_position = seek_next_skip(buffer, 2, buffer_end);
					parse_position = seek_next_nonskip(buffer, parse_position,
							buffer_end);
				}
			} else {
				// we do not have content to parse
				parse_position = 1;
			}
		}

		/*
		 * We have a previous byte in buffer.
		 * This shifts our bound forward.
		 */
		parse_bound++;
	} else {
		// we are at the beginning of the document
		parse_position = 0;
	}

	// start parsing words
	while ((parse_position < buffer_end) && (parse_position < parse_bound)) {
		long int next_parse_position = seek_next_skip(buffer, parse_position,
				buffer_end);
		long int word_length = next_parse_position - parse_position;

		// copy word to shared memory
		memcpy(shm_offset, &buffer[parse_position], sizeof(char) * word_length);
		shm_offset += word_length;

		// write terminating null byte
		*shm_offset = 0;
		shm_offset++;

		word_counter++;

		parse_position = seek_next_nonskip(buffer, next_parse_position,
				buffer_end);
	}

	*number_words = word_counter;

	// free resources
	prune_child_mem(inputfd, buffer);

	return EXIT_SUCCESS;
}

/**
 * Fills a given word to frequency map with the given words from the child.
 *
 * This function reads the parsed words from the given shared memory location.
 * The child processes must have filled the shared memory with file content
 * beforehand.
 * Each word in shared memory must be separated by exactly one null byte.
 * This function stores the parsed words in a map which uses the word as the
 * key and the frequency as the value.
 */
static void fill_table(
		std::map<const char *, int, bool (*)(const char *, const char *)> &word_table,
		const char *child_word_buffer_offset, size_t child_number_words) {
	const char *word_offset = child_word_buffer_offset;

	// fill map from word to count
	for (size_t i = 0; i < child_number_words; i++) {
		std::map<const char *, int>::iterator it = word_table.find(word_offset);

		if (it != word_table.end()) {
			it->second++;
		} else {
			word_table.insert(std::pair<const char *, int>(word_offset, 1));
		}

		word_offset += strlen(word_offset) + 1;
	}
}

/**
 * Parent process aggregates results, after child processes had finished.
 * The given table maps from words to their frequency.
 * The parent process builds an array from the map that it sorts in descending frequency order.
 * Finally, the parent process writes the results into a file.
 */
static int aggregate_results(const char *outputfname,
		std::map<const char *, int, bool (*)(const char *, const char *)> &word_table) {
	word_count *words = NULL;
	size_t different_words = word_table.size();
	FILE *outputfd = NULL;

	// create word array
	words = (word_count *) malloc(sizeof(word_count) * different_words);

	// fill word array
	{
		size_t i;
		std::map<const char *, int>::iterator it;

		for (i = 0, it = word_table.begin(); i < different_words; i++, it++) {
			words[i].word = it->first;
			words[i].count = it->second;
		}
	}

	// sort words according to count in descending order
	qsort(words, different_words, sizeof(word_count), cmp_int_desc);

	// write output file
	outputfd = fopen(outputfname, "w");

	for (size_t i = 0; i < different_words; i++) {
		int written = fprintf(outputfd, "%s\t%d\n", words[i].word,
				words[i].count);

		if (written < 0) {
			fclose(outputfd);
			free(words);
			return EXIT_FAILURE;
		}
	}

	// free resources
	fclose(outputfd);
	free(words);

	return EXIT_SUCCESS;
}

/**
 * Main program. Let child processes parse the input file.
 * Parent process aggregates results and writes them to an output file.
 */
int main(int argc, char *argv[]) {
	int no_childs = -1;
	FILE * inputfd = NULL;
	const char * inputfname = NULL;
	const char * outputfname = NULL;
	long int inputfs = -1;
	size_t chars_per_child = 0;
	int shmid = -1;
	char *shm = NULL;
	char **child_word_buffer_offsets = NULL;
	size_t **child_number_words_offsets = NULL;
	size_t child_word_buffer_length = 0;
	int error = 0;
	int opt = -1;
	std::map<pid_t, int> cpid_table;

	// set arguments to default values
	no_childs = DEFAULT_NUMBER_CHILDS;
	inputfname = DEFAULT_INPUT_FILE;
	outputfname = DEFAULT_OUTPUT_FILE;

	// argument parsing
	while ((opt = getopt(argc, argv, "p:i:o:")) != -1) {
		switch (opt) {
		case 'p':
			no_childs = atoi(optarg);
			if (no_childs < 1) {
				fprintf(stderr, "parallelism must be at least one!\n");
				exit(EXIT_FAILURE);
			}
			break;

		case 'i':
			inputfname = optarg;
			break;

		case 'o':
			outputfname = optarg;
			break;

		default: /* '?' */
			fprintf(stderr,
					"Usage: %s [-p <parallelism>] [-i <input file>] [-o <output file>]\n",
					argv[0]);
			exit(EXIT_FAILURE);
			break;
		}
	}

	// get file size
	inputfd = fopen(inputfname, "r");
	if (!inputfd) {
		fprintf(stderr, "Could not open input file!\n");
		exit(EXIT_FAILURE);
	}
	fseek(inputfd, 0, SEEK_END);
	inputfs = ftell(inputfd);
	fclose(inputfd);

	if (inputfs < 0) {
		fprintf(stderr, "Cannot obtain input file size!\n");
		exit(EXIT_FAILURE);
	} else if (inputfs == 0) {
		fprintf(stdout, "Input file is empty. We are done.\n");
		exit(EXIT_SUCCESS);
	} else {
		// inputfs > 0
		if (inputfs < no_childs) {
			no_childs = inputfs;
		}
	}

	fprintf(stdout,
			"Starting word frequency count using the following options:\n\nParallelism: %d\nInput file: %s\nOutput file: %s\n",
			no_childs, inputfname, outputfname);

	chars_per_child = (size_t) (inputfs / no_childs + 1);

	/*
	 * Allocate shared memory.
	 * Leave space for chars to parse per + MAX_WORD_LENGTH per child.
	 * As a child parses the last word only if the last word starts before
	 * or at the chars_per_child - 1, we have a space for an ending null
	 * byte.
	 * Additionally, leave space for number of parsed words per child.
	 */
	child_word_buffer_length = sizeof(char)
			* (chars_per_child + MAX_WORD_LENGTH);
	shmid = shmget(IPC_PRIVATE,
			no_childs * (child_word_buffer_length + sizeof(size_t)),
			S_IRUSR | S_IWUSR);

	if (shmid < 0) {
		perror("shmget");
		exit(EXIT_FAILURE);
	}

	// attach the shared memory
	shm = (char *) shmat(shmid, NULL, 0);

	if (shm == (char *) -1) {
		perror("shmat");
		exit(EXIT_FAILURE);
	}

	// compute offsets
	child_word_buffer_offsets = (char **) malloc(sizeof(char *) * no_childs);
	child_number_words_offsets = (size_t **) malloc(
			sizeof(size_t *) * no_childs);

	if (!child_word_buffer_offsets || !child_number_words_offsets) {
		fprintf(stdout, "Not enough memory!\n");
		prune_parent_mem(shm, shmid, child_word_buffer_offsets,
				child_number_words_offsets);
		exit(EXIT_FAILURE);
	}

	for (int i = 0; i < no_childs; i++) {
		child_word_buffer_offsets[i] = shm + i * child_word_buffer_length;
		child_number_words_offsets[i] = ((size_t *) (shm
				+ no_childs * child_word_buffer_length)) + i;
	}

	// create child processes
	for (int i = 0; i < no_childs; i++) {
		pid_t cpid = fork();

		if (cpid == -1) {
			perror("fork");
			prune_parent_mem(shm, shmid, child_word_buffer_offsets,
					child_number_words_offsets);
			exit(EXIT_FAILURE);
		}

		if (cpid == 0) {
			// child code
			int status;
			char *word_buffer_offset = child_word_buffer_offsets[i];
			size_t *number_words_offset = child_number_words_offsets[i];

			// free unused memory inherited from parent
			free(child_word_buffer_offsets);
			free(child_number_words_offsets);

			status = child_parse(inputfname, i * chars_per_child,
					(i + 1) * chars_per_child, word_buffer_offset,
					number_words_offset);

			// break loop: child must not fork child processes.
			_exit(status);
		} else {
			// parent code
			cpid_table.insert(std::pair<pid_t, int>(cpid, i));
		}
	}

	// parent code
	{
		std::map<const char *, int, bool (*)(const char *, const char *)> word_table(
				cmp_str);

		/*
		 * Child processes forked.
		 * Wait for child processes to finish.
		 */
		for (int i = 0; i < no_childs; i++) {
			int status;
			pid_t cpid;

			cpid = wait(&status);

			if (!WIFEXITED(status) || (WEXITSTATUS(status) != EXIT_SUCCESS)) {
				fprintf(stderr, "Child exited with an error!\n");
				error = 1;
			} else if (!error) {
				/*
				 * Current child and all previous children terminated
				 * successfully thus far.
				 * Fill word table with parsed words.
				 */
				int i = cpid_table.find(cpid)->second;

				fill_table(word_table, child_word_buffer_offsets[i],
						*child_number_words_offsets[i]);
			}
		}

		if (error) {
			fprintf(stderr,
					"At least one child did not terminate properly. Exiting!\n");
			prune_parent_mem(shm, shmid, child_word_buffer_offsets,
					child_number_words_offsets);
			exit(EXIT_FAILURE);
		}

		/*
		 * Aggregate results.
		 */
		error = aggregate_results(outputfname, word_table);
	}

	// free memory
	prune_parent_mem(shm, shmid, child_word_buffer_offsets,
			child_number_words_offsets);

	return error;
}
