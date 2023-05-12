#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <byteswap.h>

#include "rlegen.h"

//#define RLE_DEBUG
#ifdef RLE_DEBUG
	#define pr_debug(fmt, args...) fprintf(stderr, fmt, ## args)
#else
	#define pr_debug(fmt, args...)
#endif

#if defined(__BYTE_ORDER)
  #if __BYTE_ORDER == __BIG_ENDIAN
  #define USE_BIG_ENDIAN
  #endif
#else
  #warning "The little-endian is used by default!"
#endif

#define RLE_HEADER 0x2E454C52 // The header is 'RLE.'

#define REPET_ALPHA

/* 0xaabbccdd */
#define TO_WORD(a, b, c, d) (((a) & 0xFF) << 24 | ((b) & 0xFF) << 16 | ((c) & 0xFF) << 8 | ((d) & 0xFF))

#define OFF_HEADER 0
#define OFF_LENGTH 4
#define OFF_DATA   8

#define F_EXT  0x80 // Length extension flag
#define F_REPE 0x40 // Data duplication flag
#define F_WORD 0x20 // Word data flag, default is byte

#define FLAG_NON_MASK  0xC0
#define FLAG_REPE_MASK 0xF0
#define LEN_NON_MASK  0x3F
#define LEN_REPE_MASK 0xF
#define EXT_MASK 0x7F // bit 7 used to store F_EXT flag

#define MAX_LEN_NON  0x7FFFFFF
#define MAX_LEN_REPE 0x3FFFFFF

#ifdef REPET_ALPHA
#define F_ALPH 0x10 // Alpha data duplication flag
#define ALPHA_MASK 0x000000FF // RGBA8888 format

#undef MAX_LEN_REPE
#define MAX_LEN_REPE 0x1FFFFFF
#endif

static int padding(unsigned char flag, unsigned int len, unsigned int *ptoken)
{
	unsigned int max_len, token = 0;
	int ret = ERROR_UNKNOWN;

	if (!(flag & F_REPE))
		max_len = MAX_LEN_NON;
	else
		max_len = MAX_LEN_REPE;
		
	if (len <= (max_len >> 21)) {
		token = flag | len;
		ret = 1;
	} else if (len <= (max_len >> 14)) {
		token = TO_WORD(0, 0, (len & EXT_MASK), (flag | F_EXT | (len >> 7)));
		ret = 2;
	} else if (len <= (max_len >> 7)) {
		token = TO_WORD(0, (len & EXT_MASK), (F_EXT | ((len >> 7) & EXT_MASK)), (flag | F_EXT | (len >> 14)));
		ret = 3;
	} else if (len <= max_len) {
		token = TO_WORD((len & EXT_MASK), (F_EXT | ((len >> 7) & EXT_MASK)), (F_EXT | ((len >> 14) & EXT_MASK)), (flag | F_EXT | (len >> 21)));
		ret = 4;
	} else {
		pr_debug("ERROR: Length bits overflow! flag=0x%02x len=0x%08x\n\r", flag, len);

		return ERROR_OVERFLOW;
	}

#ifdef USE_BIG_ENDIAN
	token = bswap_32(token);
#endif
	*ptoken = token;

	return ret;
}

static int extract(unsigned char *pflag, unsigned int *plen, unsigned int token)
{
	unsigned char mask;
	int ret = ERROR_UNKNOWN;	
	
	if (token & F_REPE) {
		*pflag = token & FLAG_REPE_MASK;
		mask = LEN_REPE_MASK;
	} else {
		*pflag = token & FLAG_NON_MASK;
		mask = LEN_NON_MASK;
	}

	if (!(token & F_EXT)) {
		*plen = token & mask;
		ret = 1;
	} else if (!(token & (F_EXT << 8))) {
		*plen = (token & mask) << 7 | ((token >> 8) & EXT_MASK);
		ret = 2;
	} else if (!(token & (F_EXT << 16))) {
		*plen = (token & mask) << 14 | ((token >> 8) & EXT_MASK) << 7 | ((token >> 16) & EXT_MASK);
		ret = 3;
	} else {
		*plen = (token & mask) << 21 | ((token >> 8) & EXT_MASK) << 14 | ((token >> 16) & EXT_MASK) << 7 | ((token >> 24) & EXT_MASK);
		ret = 4;
	}

	return ret;
}

static int repet_count(char *buf, int len, unsigned char *pflag)
{
	unsigned int *pword = NULL;
	int i;

	// pword should be 4-bytes aligned
	if (!((unsigned long)buf & 0x3)) {
		pword = (unsigned int *)buf;
	}

	// Try to find duplication word data
	if (pword) {
		for (i = 1; i < (len >> 2); i++) {
			if (*pword != *(pword + i))
				break;
		}

		if (i > 1) {
			// Check if this word is byte duplication
			if (((*pword) >> 16) == ((*pword) & 0xFFFF) &&
				(((*pword) >> 8) & 0xFF) == ((*pword) & 0xFF)) {
				*pflag = F_REPE;

				return (i * 4);
			} else {
				*pflag = F_REPE | F_WORD;

				return i;
			}
		}
	}

	// Try to find duplication byte data
	for (i = 1; i < len; i++) {
		if (buf[0] != buf[i])
			break;
	}

	if (i > 2) {
		*pflag = F_REPE;

		return i;
	}

#ifdef REPET_ALPHA
	// Try to find duplication alpha data
	if (pword) {
		for (i = 1; i < (len >> 2); i++) {
			if ((*pword & ALPHA_MASK) != (*(pword + i) & ALPHA_MASK))
				break;

			if (*(pword + i - 1) == *(pword + i)) {
				i--;
				break;
			}
		}

		if (i > 2) {
			*pflag = F_REPE | F_ALPH;

			return i;
		}
	}
#endif

	// Now count the non duplicate data
	for (i = 3; i < len; i++) {
		// Check by bytes
		if ((buf[i-2] == buf[i-1]) &&
			(buf[i-1] == buf[i])) {
			i -= 2;
			break;
		}

		// Check by words
		if ((i >= 7) && (((unsigned long)&buf[i] & 0x3) == 0x3)) {
			if ((buf[i-7] == buf[i-3]) && 
				(buf[i-6] == buf[i-2]) &&
				(buf[i-5] == buf[i-1]) &&
				(buf[i-4] == buf[i])) {
				i -= 7;
				break;
			}
		}

#ifdef REPET_ALPHA
		if ((i >= 15) && (((unsigned long)&buf[i] & 0x3) == 0x3)) {
			pword = (unsigned int *)&buf[i];
			if ((buf[i-11] == buf[i-3]) && (buf[i-7] == buf[i-3]) &&
				(*pword) != *(pword - 1)) {
				i -= 11;
				break;
			}
		}
#endif
	}

	*pflag = 0;
	return i;
}

unsigned int rle_header(char *buffer, int length)
{
	if (length <= OFF_DATA)
		return ERROR_LENGTH;

	return TO_WORD(buffer[OFF_HEADER + 3], buffer[OFF_HEADER + 2], buffer[OFF_HEADER + 1], buffer[OFF_HEADER]);
}

unsigned int rle_length(char *buffer, int length)
{
	if (length <= OFF_DATA)
		return ERROR_LENGTH;

    return TO_WORD(buffer[OFF_LENGTH + 3], buffer[OFF_LENGTH + 2], buffer[OFF_LENGTH + 1], buffer[OFF_LENGTH]);
}

int rle_compress(char *buffer, int length, char *out_buf, FILE *out_fp)
{
	int off = 0;
	int ret;
	int i;
	unsigned char flag;
	unsigned int token, len, data;
	int total = 0;

	// write rle header and size
#ifndef USE_BIG_ENDIAN
	data = RLE_HEADER;
#else
	data = bswap_32(RLE_HEADER);
#endif
	if (out_fp)
		fwrite((void *)&data, sizeof(int), 1, out_fp);

#ifndef USE_BIG_ENDIAN
	data = length;
#else
	data = bswap_32(length);
#endif
	if (out_fp)
		fwrite((void *)&data, sizeof(int), 1, out_fp);

	while (off < length) {
		len = repet_count(&buffer[off], length - off, &flag);
		if (len <= 0)
			return ERROR_UNKNOWN;

		/* Generate the token */
		ret = padding(flag, len, &token);
		pr_debug("  0x%08x: %08x flag=%02x len=0x%08x %d ret=%d\n\r", off, bswap_32(token), flag, len, len, ret);
		if (ret <= 0)
			return ret;

		if (flag & F_REPE) {
			if (flag & F_WORD) {
				fwrite((void *)&token, ret, 1, out_fp);
				fwrite((void *)&buffer[off], sizeof(int), 1, out_fp);

				off += len * 4;
				total += ret + 4;
			} else if (flag & F_ALPH) {
				fwrite((void *)&token, ret, 1, out_fp);

				data = *(unsigned int*)&buffer[off];
#ifdef USE_BIG_ENDIAN
				fwrite((void *)&buffer[off + 3], sizeof(char), 1, out_fp);
#else
				fwrite((void *)&buffer[off], sizeof(char), 1, out_fp);
#endif

				for (i = 0; i < len; i++) {
					data = *(unsigned int*)&buffer[off + i * 4];
#ifndef USE_BIG_ENDIAN
					data = bswap_32(data);
#endif
					fwrite((void *)&data, sizeof(int) - 1, 1, out_fp);
				}

				off += len * 4;
				total += ret + 1 + len * 3; 
			} else {
				fwrite((void *)&token, ret, 1, out_fp);
				fwrite((void *)&buffer[off], sizeof(char), 1, out_fp);

				off += len;
				total += ret + 1;
			}
		} else {
			fwrite((void *)&token, ret, 1, out_fp);
			fwrite((void *)&buffer[off], len, 1, out_fp);

			off += len;
			total += ret + len;
		}
	}

	pr_debug("Compress total %d\n\r", total);

	return total;
}

int rle_decompress(char *buffer, int length, char *out_buf, FILE *out_fp)
{
	int off = OFF_DATA;
	int ret;
	int i;
	unsigned char flag;
	unsigned int token, len, data;
	unsigned int rle_len;
	int total = 0;

	if (length <= OFF_DATA)
		return ERROR_LENGTH;

	data = TO_WORD(buffer[OFF_HEADER + 3], buffer[OFF_HEADER + 2], buffer[OFF_HEADER + 1], buffer[OFF_HEADER]);
	if (data != RLE_HEADER)
		return ERROR_RLE_HEADER;

	rle_len = TO_WORD(buffer[OFF_LENGTH + 3], buffer[OFF_LENGTH + 2], buffer[OFF_LENGTH + 1], buffer[OFF_LENGTH]);

	while (off < length) {
		/* Parse the token */
		token = TO_WORD(buffer[off + 3], buffer[off + 2], buffer[off + 1], buffer[off]);
		ret = extract(&flag, &len, token);
		pr_debug("  0x%08x: %08x flag=%02x len=0x%08x %d ret=%d\n\r", off, bswap_32(token), flag, len, len, ret);
		if (ret <= 0)
			return ret;

		if (flag & F_REPE) {
			if (flag & F_WORD) {
				data = TO_WORD(buffer[off + ret + 3], buffer[off + ret + 2], buffer[off + ret + 1], buffer[off + ret]);

				for (i = 0; i < len; i++) {
					if (out_fp)
						fwrite((void *)&data, sizeof(int), 1, out_fp);
					if (out_buf)
						((unsigned int *)out_buf)[(total >> 2) + i] = data;
				}

				off += ret + 4;
				total += len * 4;
			} else if (flag & F_ALPH) {
				data = buffer[off + ret];

				for (i = 0; i < len; i++) {
					data = TO_WORD(buffer[off + ret + i*3 + 1], buffer[off + ret + i*3 + 2], buffer[off + ret + i*3 + 3], data & 0xff);

					if (out_fp)
						fwrite((void *)&data, sizeof(int), 1, out_fp);
					if (out_buf)
						((unsigned int *)out_buf)[(total >> 2) + i] = data;
				}

				off += ret + 1 + len * 3;
				total += len * 4;
			} else {
				for (i = 0; i < len; i++) {
					if (out_fp)
						fwrite((void *)&buffer[off + ret], sizeof(char), 1, out_fp);
					if (out_buf)
						out_buf[total + i] = buffer[off + ret];
				}

				off += ret + 1;
				total += len;
			}
		} else {
			for (i = 0; i < len; i++) {
				if (out_fp)
					fwrite((void *)&buffer[off + ret + i], sizeof(char), 1, out_fp);
				if (out_buf)
					out_buf[total + i] = buffer[off + ret + i];
			}

			off += ret + len;
			total += len;
		}
	}

	pr_debug("Decompress total %d\n\r", total);

	if (total != rle_len) {
		return ERROR_RLE_LENGTH;
	}

	return total;
}
