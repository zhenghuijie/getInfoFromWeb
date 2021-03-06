#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "log.h"
#include "util.h"
#include "http.h"

ssize_t base64_decode (const char *base64, void *dest);
size_t base64_encode (const void *data, size_t length, char *dest);
unsigned char * base64_decode_lighttpd(char *out, const char *in);
ssize_t base64_decode_for_big_buffer_to_file(const char *base64, int fd);
#if 0
int main(int argc, char *argv)
{
	char data[1024] = "ZhengHuijieLoveZhengJIn", dest[1024] = "";
	char out_for_lighttpd[1024] = "";
	base64_encode(data, strlen(data), dest);
	printf("after encode: %s\n", dest);
base64_decode_lighttpd(out_for_lighttpd, dest);
printf("decode: %s\n", out_for_lighttpd);
	return 0;
}
#endif
size_t base64_encode (const void *data, size_t length, char *dest)
{
  /* Conversion table.  */
  static const char tbl[64] = {
    'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P',
    'Q','R','S','T','U','V','W','X','Y','Z','a','b','c','d','e','f',
    'g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v',
    'w','x','y','z','0','1','2','3','4','5','6','7','8','9','+','/'
  };
  /* Access bytes in DATA as unsigned char, otherwise the shifts below
     don't work for data with MSB set. */
  const unsigned char *s = data;
  /* Theoretical ANSI violation when length < 3. */
  const unsigned char *end = (const unsigned char *) data + length - 2;
  char *p = dest;

  /* Transform the 3x8 bits to 4x6 bits, as required by base64.  */
  for (; s < end; s += 3)
    {
      *p++ = tbl[s[0] >> 2];
      *p++ = tbl[((s[0] & 3) << 4) + (s[1] >> 4)];
      *p++ = tbl[((s[1] & 0xf) << 2) + (s[2] >> 6)];
      *p++ = tbl[s[2] & 0x3f];
    }

  /* Pad the result if necessary...  */
  switch (length % 3)
    {
    case 1:
      *p++ = tbl[s[0] >> 2];
      *p++ = tbl[(s[0] & 3) << 4];
      *p++ = '=';
      *p++ = '=';
      break;
    case 2:
      *p++ = tbl[s[0] >> 2];
      *p++ = tbl[((s[0] & 3) << 4) + (s[1] >> 4)];
      *p++ = tbl[((s[1] & 0xf) << 2)];
      *p++ = '=';
      break;
    }
  /* ...and zero-terminate it.  */
  *p = '\0';

  return p - dest;
}


ssize_t base64_decode (const char *base64, void *dest)
{
  /* Table of base64 values for first 128 characters.  Note that this
     assumes ASCII (but so does Wget in other places).  */
  static const signed char base64_char_to_value[128] =
    {
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*   0-  9 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  10- 19 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  20- 29 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  30- 39 */
      -1,  -1,  -1,  62,  -1,  -1,  -1,  63,  52,  53,  /*  40- 49 */
      54,  55,  56,  57,  58,  59,  60,  61,  -1,  -1,  /*  50- 59 */
      -1,  -1,  -1,  -1,  -1,  0,   1,   2,   3,   4,   /*  60- 69 */
      5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  /*  70- 79 */
      15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  /*  80- 89 */
      25,  -1,  -1,  -1,  -1,  -1,  -1,  26,  27,  28,  /*  90- 99 */
      29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  /* 100-109 */
      39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  /* 110-119 */
      49,  50,  51,  -1,  -1,  -1,  -1,  -1             /* 120-127 */
    };
#define BASE64_CHAR_TO_VALUE(c) ((int) base64_char_to_value[c])
#define IS_BASE64(c) ((IS_ASCII (c) && BASE64_CHAR_TO_VALUE (c) >= 0) || c == '=')

  const char *p = base64;
  char *q = dest;

  while (1)
    {
      unsigned char c;
      unsigned long value;

      /* Process first byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        break;
      if (c == '=' || !IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */
      value = BASE64_CHAR_TO_VALUE (c) << 18;

      /* Process second byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (c == '=' || !IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */
      value |= BASE64_CHAR_TO_VALUE (c) << 12;
      *q++ = value >> 16;

      /* Process third byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (!IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */

      if (c == '=')
        {
          NEXT_CHAR (c, p);
          if (!c)
            return -1;          /* premature EOF while decoding base64 */
          if (c != '=')
            return -1;          /* padding `=' expected but not found */
          continue;
        }

      value |= BASE64_CHAR_TO_VALUE (c) << 6;
      *q++ = 0xff & value >> 8;

      /* Process fourth byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (c == '=')
        continue;
      if (!IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */

      value |= BASE64_CHAR_TO_VALUE (c);
      *q++ = 0xff & value;
    }
#undef IS_BASE64
#undef BASE64_CHAR_TO_VALUE

  return q - (char *) dest;
}



ssize_t base64_decode_for_big_buffer_to_file(const char *base64, int fd) {
  /* Table of base64 values for first 128 characters.  Note that this
     assumes ASCII (but so does Wget in other places).  */
  static const signed char base64_char_to_value[128] =
    {
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*   0-  9 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  10- 19 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  20- 29 */
      -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  -1,  /*  30- 39 */
      -1,  -1,  -1,  62,  -1,  -1,  -1,  63,  52,  53,  /*  40- 49 */
      54,  55,  56,  57,  58,  59,  60,  61,  -1,  -1,  /*  50- 59 */
      -1,  -1,  -1,  -1,  -1,  0,   1,   2,   3,   4,   /*  60- 69 */
      5,   6,   7,   8,   9,   10,  11,  12,  13,  14,  /*  70- 79 */
      15,  16,  17,  18,  19,  20,  21,  22,  23,  24,  /*  80- 89 */
      25,  -1,  -1,  -1,  -1,  -1,  -1,  26,  27,  28,  /*  90- 99 */
      29,  30,  31,  32,  33,  34,  35,  36,  37,  38,  /* 100-109 */
      39,  40,  41,  42,  43,  44,  45,  46,  47,  48,  /* 110-119 */
      49,  50,  51,  -1,  -1,  -1,  -1,  -1             /* 120-127 */
    };
#define BASE64_CHAR_TO_VALUE(c) ((int) base64_char_to_value[c])
#define IS_BASE64(c) ((IS_ASCII (c) && BASE64_CHAR_TO_VALUE (c) >= 0) || c == '=')

  const char *p = base64;
  unsigned char cache[1024] = "";
  unsigned char *q = cache;
  int write_size = 0;

  while (1)
    {
      unsigned char c;
      unsigned long value;

      /* Process first byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        break;
      if (c == '=' || !IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */
      value = BASE64_CHAR_TO_VALUE (c) << 18;

      /* Process second byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (c == '=' || !IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */
      value |= BASE64_CHAR_TO_VALUE (c) << 12;
      *q++ = value >> 16;

      /* Process third byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (!IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */

      if (c == '=')
        {
          NEXT_CHAR (c, p);
          if (!c)
            return -1;          /* premature EOF while decoding base64 */
          if (c != '=')
            return -1;          /* padding `=' expected but not found */
          continue;
        }

      value |= BASE64_CHAR_TO_VALUE (c) << 6;
      *q++ = 0xff & value >> 8;

      /* Process fourth byte of a quadruplet.  */
      NEXT_CHAR (c, p);
      if (!c)
        return -1;              /* premature EOF while decoding base64 */
      if (c == '=')
        continue;
      if (!IS_BASE64 (c))
        return -1;              /* illegal char while decoding base64 */

      value |= BASE64_CHAR_TO_VALUE (c);
      *q++ = 0xff & value;
      if(q - cache >= sizeof(cache) - 4)
      {
		  write_all(fd, (void *)cache, (size_t)(q - cache));
	      write_size += q - cache;
	      q = cache;
      }
    }
  if(q - cache > 0)
      {
	  	  write_all(fd, cache, q - cache);
	      write_size += q - cache;
	      q = cache;
      }
#undef IS_BASE64
#undef BASE64_CHAR_TO_VALUE

  return write_size;
}

unsigned char * base64_decode_lighttpd(char *out, const char *in) {
	unsigned char *result;
	int ch, j = 0, k;
	size_t i;

	size_t in_len = strlen(in);
	static const char base64_pad = '=';

	static const short base64_reverse_table[256] = {
	        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, //16
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 62/*+*/, -1, -1, -1, 63,
		        52, 53, 54, 55, 56, 57, 58, 59, 60, 61, -1, -1, -1, -1, -1, -1,
		        -1,  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14,
		        15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
		        -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40,
		        41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
		        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};


	result = (unsigned char *)out;

	ch = in[0];
	/* run through the whole string, converting as we go */
	for (i = 0; i < in_len; i++) {
		ch = in[i];

		if (ch == '\0') break;

		if (ch == base64_pad) break;

		ch = base64_reverse_table[ch];
		if (ch < 0) continue;

		switch(i % 4) {
		case 0:
			result[j] = ch << 2;
			break;
		case 1:
			result[j++] |= ch >> 4;
			result[j] = (ch & 0x0f) << 4;
			break;
		case 2:
			result[j++] |= ch >>2;
			result[j] = (ch & 0x03) << 6;
			break;
		case 3:
			result[j++] |= ch;
			break;
		}
	}
	k = j;
	/* mop things up if we ended on a boundary */
	if (ch == base64_pad) {
		switch(i % 4) {
		case 0:
		case 1:
			return NULL;
		case 2:
			k++;
		case 3:
			result[k++] = 0;
		}
	}
	result[k] = '\0';

	return result;
}

/*
 * when successful return 0 or -1
 * */
int base64_encode_to_file(const char *in_file, const char *out_file)
{
#undef MAX_MALLOC_FILE_SIZE
#undef MAX_READ_CHAR
#define MAX_MALLOC_FILE_SIZE (100 * 1024 * 1024)
#define MAX_READ_CHAR (100 * 1024)
	int fd = -1;
	char *p = NULL, *output_p = NULL;
	struct stat file_stat;
	int outputSize = -1;

	if(in_file == NULL || out_file == NULL)
	{
		printf("need input file name and outputFileName\n");
		goto ERR;
	}
	if((fd = open(in_file, O_RDONLY)) < 0)
	{
		printf("open error: %s\n", strerror(errno));
		goto ERR;
	}
	if(fstat(fd, &file_stat) < 0)
	{
		printf("fstat error: %s\n", strerror(errno));
		goto ERR;
	}
	if(file_stat.st_size <= 0)
	{
		printf("file is empty\n");
		goto ERR;
	}
	if(!S_ISREG(file_stat.st_mode))
	{
		printf("Just support regulal file\n");
		goto ERR;
	}
	if(MAP_FAILED == (p = mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0)))
	{
		printf("mmap error: %s\n", strerror(errno));
		goto ERR;
	}
	close(fd);
	fd = -1; //in case double free

	//to small file
	if(file_stat.st_size < MAX_MALLOC_FILE_SIZE)
	{
		if((output_p = malloc(1.5 * file_stat.st_size)) == NULL)
		{
			printf("malloc error: %s\n", strerror(errno));
			goto ERR;
		}
		output_p[0] = '\0';
		outputSize = base64_encode (p, file_stat.st_size, output_p);
		if(MAP_FAILED != p && NULL != p)
		{
			munmap(p, file_stat.st_size);
			p = NULL;
		}
		if((fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0777)) < 0)
		{
			printf("open %s error\n", strerror(errno));
			goto ERR;
		}
		write_all(fd, output_p, outputSize);
		if(fd != -1)
		{
			close(fd);
			fd = -1;
		}
	}
	else
	{
		int read_n = 0, file_size = file_stat.st_size;
		char *save_p = p;
		if((fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0777)) < 0)
		{
			printf("open error: %s\n", strerror(errno));
			goto ERR;
		}
		if((output_p = malloc(1.5 * MAX_READ_CHAR)) == NULL)
		{
			printf("malloc error: %s\n", strerror(errno));
			goto ERR;
		}
		while(file_size > 0)
		{
			if(file_size > MAX_READ_CHAR)
			{
				read_n = MAX_READ_CHAR;
			}
			else
			{
				read_n = file_size;
			}
			output_p[0] = '\0';
			/*multiple of 4*/
			outputSize = base64_encode (save_p, read_n, output_p);
			save_p += read_n;
			file_size -= read_n;
			write_all(fd, output_p, outputSize);
		}
		if(fd != -1)
		{
			close(fd);
			fd = -1;
		}
		if(MAP_FAILED != p && NULL != p)
		{
			munmap(p, file_stat.st_size);
			p = NULL;
		}
	}
	return 0;
ERR:
	if(fd != -1)
	{
		close(fd);
		fd = -1;
	}
	if(MAP_FAILED != p && NULL != p)
	{
		munmap(p, file_stat.st_size);
		p = NULL;
	}
	return -1;
}

/*
 * when successful return 0 or -1
 * */
int base64_decode_to_file(const char *in_file, const char *out_file)
{
#undef MAX_MALLOC_FILE_SIZE
#undef MAX_READ_CHAR
#define MAX_MALLOC_FILE_SIZE (100 * 1024 * 1024)
#define MAX_READ_CHAR (100 * 1024)
	int fd = -1;
	char *p = NULL, *output_p = NULL;
	struct stat file_stat;
	int outputSize = -1;
	if(in_file == NULL || out_file == NULL)
	{
		return -1;
	}
	if((fd = open(in_file, O_RDONLY)) < 0)
	{
		printf("open error: %s\n", strerror(errno));
		goto ERR;
	}
	if(fstat(fd, &file_stat) < 0)
	{
		printf("fstat error: %s\n", strerror(errno));
		goto ERR;
	}
	if(file_stat.st_size <= 0)
	{
		printf("file is empty\n");
		goto ERR;
	}
	if(MAP_FAILED == (p = mmap(0, file_stat.st_size, PROT_READ, MAP_SHARED, fd, 0)))
	{
		printf("mmap error: %s\n", strerror(errno));
		goto ERR;
	}
	close(fd);
	fd = -1;

	if(file_stat.st_size < MAX_MALLOC_FILE_SIZE)
	{
		if((output_p = malloc(file_stat.st_size)) == NULL)
		{
			printf("malloc error: %s\n", strerror(errno));
			goto ERR;
		}
		output_p[0] = '\0';
		outputSize = base64_decode (p, output_p);
		if(outputSize <= 0)
		{
			printf("decode file error\n");
			goto ERR;
		}
		if((fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0777)) < 0)
		{
			printf("open %s error\n", strerror(errno));
			goto ERR;
		}
		write_all(fd, output_p, outputSize);
	}
	else
	{
		if((fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0777)) < 0)
		{
			printf("open %s error\n", strerror(errno));
			goto ERR;
		}
		outputSize = base64_decode_for_big_buffer_to_file(p, fd);
		if(outputSize <= 0)
		{
			printf("decode error\n");
			goto ERR;
		}
	}
	if(output_p != NULL)
	{
		free(output_p);
		output_p = NULL;
	}
	if(fd != -1)
	{
		close(fd);
		fd = -1;
	}
	if(MAP_FAILED != p && NULL != p)
	{
		munmap(p, file_stat.st_size);
		p = NULL;
	}
	return 0;
ERR:
	if(output_p != NULL)
	{
		free(output_p);
		output_p = NULL;
	}
	if(fd != -1)
	{
		close(fd);
		fd = -1;
	}
	if(MAP_FAILED != p && NULL != p)
	{
		munmap(p, file_stat.st_size);
		p = NULL;
	}
	return -1;
}




#define XNUM_TO_DIGIT(x) ("0123456789ABCDEF"[x] + 0)
#define XNUM_TO_digit(x) ("0123456789abcdef"[x] + 0)
/* The reverse of the above: convert a number in the [0, 16) range to
   the ASCII representation of the corresponding hexadecimal digit.
   `+ 0' is there so you can't accidentally use it as an lvalue.  */

/* Dump the hexadecimal representation of HASH to BUF.  HASH should be
   an array of 16 bytes containing the hash keys, and BUF should be a
   buffer of 33 writable characters (32 for hex digits plus one for
   zero termination).  */
void dump_hash (char *buf, const unsigned char *hash)
{
  int i;

  for (i = 0; i < MD5_HASHLEN; i++, hash++)
    {
      *buf++ = XNUM_TO_digit (*hash >> 4);
      *buf++ = XNUM_TO_digit (*hash & 0xf);
    }
  *buf = '\0';
}

/* Take the line apart to find the challenge, and compose a digest
   authorization header.  See RFC2069 section 2.1.2.  */
char *digest_authentication_encode (const char *au, const char *user,
                              const char *passwd, const char *method,
                              const char *path)
{
  static char *realm, *opaque, *nonce, *qop;
  static struct {
    const char *name;
    char **variable;
  } options[] = {
    { "realm", &realm },
    { "opaque", &opaque },
    { "nonce", &nonce },
    { "qop", &qop }
  };
  char cnonce[16] = "";
  char *res;
  param_token name, value;

  realm = opaque = nonce = NULL;

  au += 6;                      /* skip over `Digest' */
  while (extract_param (&au, &name, &value, ','))
    {
      size_t i;
      size_t namelen = name.e - name.b;
      for (i = 0; i < countof (options); i++)
        if (namelen == strlen (options[i].name)
            && 0 == strncmp (name.b, options[i].name,
                             namelen))
          {
            *options[i].variable = strdupdelim (value.b, value.e);
            break;
          }
    }
  if (qop && strcmp (qop, "auth"))
  {
	  xfree (qop); /* force freeing mem and continue */
  }
  if (!realm || !nonce || !user || !passwd || !path || !method)
    {
      xfree_null (realm);
      xfree_null (opaque);
      xfree_null (nonce);
      return NULL;
    }

  /* Calculate the digest value.  */
  {
	MD5_CTX ctx;
    unsigned char hash[MD5_HASHLEN];
    char a1buf[MD5_HASHLEN * 2 + 1], a2buf[MD5_HASHLEN * 2 + 1];
    char response_digest[MD5_HASHLEN * 2 + 1];

    /* A1BUF = H(user ":" realm ":" password) */
    MD5_Init (&ctx);
    MD5_Update (&ctx, (unsigned char *)user, strlen (user));
    MD5_Update (&ctx, (unsigned char *)":", 1);
    MD5_Update (&ctx, (unsigned char *)realm, strlen (realm));
    MD5_Update (&ctx, (unsigned char *)":", 1);
    MD5_Update (&ctx, (unsigned char *)passwd, strlen (passwd));
    MD5_Final (hash, &ctx);
    dump_hash (a1buf, hash);

    /* A2BUF = H(method ":" path) */
    MD5_Init (&ctx);
    MD5_Update (&ctx, (unsigned char *)method, strlen (method));
    MD5_Update (&ctx, (unsigned char *)":", 1);
    MD5_Update (&ctx, (unsigned char *)path, strlen (path));
    MD5_Final (hash, &ctx);
    dump_hash (a2buf, hash);

    if (qop && !strcmp (qop, "auth"))
    {
	    /* RFC 2617 Digest Access Authentication */
	    /* generate random hex string */
	    if (!*cnonce)
	    {
	//	    snprintf (cnonce, sizeof (cnonce), "%08x",
	//			    (unsigned) random_number (INT_MAX));
			snprintf(cnonce, sizeof(cnonce), "66id03rm");
	    }

	    /* RESPONSE_DIGEST = H(A1BUF ":" nonce ":" noncecount ":" clientnonce ":" qop ": " A2BUF) */
	    MD5_Init (&ctx);
	    MD5_Update (&ctx, (unsigned char *)a1buf, MD5_HASHLEN * 2);
	    MD5_Update (&ctx, (unsigned char *)":", 1);
	    MD5_Update (&ctx, (unsigned char *)nonce, strlen (nonce));
	    MD5_Update (&ctx, (unsigned char *)":", 1);
	    MD5_Update (&ctx, (unsigned char *)"00000001", 8);/* TODO: keep track of server nonce values */
	    MD5_Update (&ctx, (unsigned char *)":", 1);
	    MD5_Update (&ctx, (unsigned char *)cnonce, strlen (cnonce));
	    MD5_Update (&ctx, (unsigned char *)":", 1);
	    MD5_Update (&ctx, (unsigned char *)qop, strlen (qop));
	    MD5_Update (&ctx, (unsigned char *)":", 1);
	    MD5_Update (&ctx, (unsigned char *)a2buf, MD5_HASHLEN * 2);

	    MD5_Final (hash, &ctx);
    }
    else
    {
    /* RESPONSE_DIGEST = H(A1BUF ":" nonce ":" A2BUF) */
    MD5_Init (&ctx);
    MD5_Update (&ctx, (unsigned char *)a1buf, MD5_HASHLEN * 2);
    MD5_Update (&ctx, (unsigned char *)":", 1);
    MD5_Update (&ctx, (unsigned char *)nonce, strlen (nonce));
    MD5_Update (&ctx, (unsigned char *)":", 1);
    MD5_Update (&ctx, (unsigned char *)a2buf, MD5_HASHLEN * 2);
    MD5_Final (hash, &ctx);
    }
    dump_hash (response_digest, hash);

    res = xmalloc (strlen (user)
                   + strlen (user)
                   + strlen (realm)
                   + strlen (nonce)
                   + strlen (path)
                   + 2 * MD5_HASHLEN /*strlen (response_digest)*/
                   + (opaque ? strlen (opaque) : 0)
		   + (qop ? 128: 0)
		   + strlen (cnonce)
                   + 128);

    if (qop && !strcmp (qop, "auth"))
    {
	    sprintf (res, "Digest "\
			    "username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\""\
			    ", qop=auth, nc=00000001, cnonce=\"%s\"",
			    user, realm, nonce, path, response_digest, cnonce);
    }
    else
    {
    sprintf (res, "Digest \
username=\"%s\", realm=\"%s\", nonce=\"%s\", uri=\"%s\", response=\"%s\"",
             user, realm, nonce, path, response_digest);
    }
//    fprintf(stderr, "%s\n", res);
    if (opaque)
      {
        char *p = res + strlen (res);
        strcat (p, ", opaque=\"");
        strcat (p, opaque);
        strcat (p, "\"");
      }
  }
  xfree_null (realm);
  xfree_null (opaque);
  xfree_null (nonce);
  return res;
}



