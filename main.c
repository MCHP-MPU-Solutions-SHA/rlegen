#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>

#include "rle.h"

static void usage()
{
	fprintf(stderr,
		"Usage: rle [OPTION] <INFILE> [-o <OUTFILE>]\n"
		"Compress or decompress binary file with RLE\n"
		"\n"
		"Options:\n"
		"  -d, --decompress 	Decompress RLE file\n"
		"  -o, --output=file	Output to the specified file\n");
}

int main(int argc, char **argv) {
	int ret;
	int length, decompress = 0;
	FILE *fp_in, *fp_out = NULL;
	char *buf_in = NULL;

	static struct option options[] = {
		{"decompress",  no_argument      , NULL, 'd'},
		{"output"    ,  required_argument, NULL, 'o'},
		{0, 0, 0, 0}
	};
	char *opt_string   = "do:";

	while ((ret = getopt_long(argc, argv, opt_string, options, NULL)) >= 0) {
		switch(ret) {
			case 'd':
				decompress = 1;
				break;
			case 'o':
				fp_out = fopen(optarg, "wb");
				if (fp_out == NULL) {
					fprintf(stderr, "ERROR: Failed to open output file %s!\n", optarg);
					exit(EXIT_FAILURE);
				}
				break;
			default:
				exit(EXIT_FAILURE);
		}
	}

	if ((argc < 2) || (argc < (optind + 1))) {
		usage();
		return -1;
	}
	
	fp_in = fopen(argv[optind], "r");
	if (fp_in == NULL) {
		fprintf(stderr, "ERROR: Failed to open input file %s!\n", argv[optind]);
		exit(EXIT_FAILURE);
	}

	fseek(fp_in, 0, SEEK_END);
	length = ftell(fp_in);
	fseek(fp_in, 0, SEEK_SET);

	buf_in = malloc(length);
	if (!buf_in) {
		fprintf(stderr, "ERROR: Failed to malloc input buffer!\n");
		exit(EXIT_FAILURE);
	}

	ret = fread(buf_in, length, 1, fp_in);
	if (!ret) {
		fprintf(stderr, "ERROR: Failed to read input file!\n");
		exit(EXIT_FAILURE);
	}

	/* Output to stdout if not speccified */
	if (!fp_out)
		fp_out = freopen(NULL, "wb", stdout);
	if (!fp_out) {
		fprintf(stderr, "ERROR: Failed to setup output stream!\n");
		exit(EXIT_FAILURE);
	}
	

	if (decompress)
		ret = rle_decompress(buf_in, length, NULL, fp_out);
	else
		ret = rle_compress(buf_in, length, NULL, fp_out);

	if (ret < 0) {
		switch (ret) {
		case ERROR_UNKNOWN:
			fprintf(stderr, "ERROR: Unknow error occurs when processing data.\n\r");
			break;

		case ERROR_LENGTH:
			fprintf(stderr, "ERROR: The size of rle file is too samll, length %d\n\r", length);
			break;

		case ERROR_OVERFLOW:
			fprintf(stderr, "ERROR: Data package length overflow when padding.\n\r");
			break;

		case ERROR_RLE_HEADER:
			fprintf(stderr, "ERROR: RLE header mismatched, 0x%08x\n\r", rle_header(buf_in, length));
			break;

		case ERROR_RLE_LENGTH:
			fprintf(stderr, "ERROR: Decompressed data size mismached, rle length %d\n\r", rle_length(buf_in, length));
			break;

		default:
			fprintf(stderr, "ERROR: Unexpected error number %d\n\r", ret);
			break;
		}
	
		exit(EXIT_FAILURE);
	}

	return ret;
}
