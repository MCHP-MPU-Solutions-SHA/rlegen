#ifndef _RLE_H
#define _RLE_H

#define ERROR_UNKNOWN		-1
#define ERROR_LENGTH		-2
#define ERROR_OVERFLOW		-3
#define ERROR_RLE_HEADER	-4
#define ERROR_RLE_LENGTH	-5

unsigned int rle_header(char *buffer, int length);
unsigned int rle_length(char *buffer, int length);

int rle_compress(char *buffer, int length, char *out_buf, FILE *out_fp);
int rle_decompress(char *buffer, int length, char *out_buf, FILE *out_fp);

#endif /* _RLE_H */
