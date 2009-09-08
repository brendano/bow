/* libbow.h - public declarations for the Bag-Of-Words Library, libbow.
   Copyright (C) 1997, 1998, 1999, 2000 Andrew McCallum

   Written by:  Andrew Kachites McCallum <mccallum@cs.cmu.edu>

   This file is part of the Bag-Of-Words Library, `libbow'.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License
   as published by the Free Software Foundation, version 2.
   
   This library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA */

/* Pronounciation guide: "libbow" rhymes with "lib-low", not "lib-cow". */

#ifndef __libbow_h_INCLUDE
#define __libbow_h_INCLUDE

/* These next two macros are automatically maintained by the Makefile,
   in conjunction with the file ./Version. */
#define BOW_MAJOR_VERSION 1
#define BOW_MINOR_VERSION 0
#define BOW_VERSION BOW_MAJOR_VERSION.BOW_MINOR_VERSION

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <math.h>
#include <assert.h>
#include <sys/types.h>		/* for netinet/in.h on SunOS */
#include <sys/stat.h>
#include <netinet/in.h>		/* for machine-independent byte-order */
#include <stdlib.h>             /* For malloc() etc. on DEC Alpha */
#include <string.h>		/* for strlen() on DEC Alpha */
#include <limits.h>		/* for PATH_MAX and SHRT_MAX and friends */
#include <float.h>		/* for FLT_MAX and friends */
#include <unistd.h>		/* for SEEK_SET and friends on SunOS */
#if BOW_MCHECK
#include <mcheck.h>
#endif /* BOW_MCHECK */

#include "argp/argp.h"

#if defined (Windows_NT) && OS == Windows_NT
#define htonl(a) (a)
#define htons(a) (a)
#define ntohl(a) (a)
#define ntohs(a) (a)
#endif

#ifdef __linux__
#undef assert
#define assert(expr)   \
((void) ((expr) || (bow_error ("Assertion failed %s:%d:" __STRING(expr), __FILE__, __LINE__), NULL)))
#endif

#if !(HAVE_SRANDOM && HAVE_RANDOM) /* for SunOS */
#undef srandom
#define srandom srand
#undef random
#define random rand
#endif

#ifndef HAVE_STRCHR
#define strchr index
#endif
#ifndef HAVE_STRRCHR
#define strrchr rindex
#endif

#if !PATH_MAX			/* for SunOS */
#define PATH_MAX 255
#endif

#if !defined SEEK_SET || !defined(SEEK_CUR) /* for SunOS */
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif

#if !defined(MIN)
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#endif

#if !defined(MAX)
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
#endif

#if !defined(ABS)
#define ABS(a) (((a) < 0) ? -(a) : (a))
#endif

#ifndef STRINGIFY
#define STRINGIFY(s) XSTRINGIFY(s)
#define XSTRINGIFY(s) #s
#endif

typedef enum { bow_no, bow_yes } bow_boolean;

typedef struct _bow_iterator_double {
  /* Move to first item collection (optional row indicates which collection)*/
  void (*reset) (int row_index, void *context);
  /* Move to next item in collection.  Return zero at end. */
  int (*advance)(void *context);
  /* Index at current item, not necessarily contiguous.
     Returns INT_MIN when invalid. */
  int (*index) (void *context);
  /* Value at current item. */
  double (*value) (void *context);
  /* Collection-specific context */
  void *context;
} bow_iterator_double;



/* Lexing words from a file. */

#define BOW_MAX_WORD_LENGTH 4096

/* A structure for maintaining the context of a lexer.  (If you need
   to create a lexer that uses more context than this, define a new
   structure that includes this structure as its first element;
   BOW_LEX_GRAM, defined below is an example of this.)  */
typedef struct _bow_lex {
  char *document;
  int document_length;
  int document_position;
} bow_lex;

/* A lexer is represented by a pointer to a structure of this type. */
typedef struct _bow_lexer {
  int sizeof_lex;		/* The size of the bow_lex (or subclass) */
  struct _bow_lexer *next;	/* The next lexer in the "pipe-like" chain */
  /* Pointers to functions for opening, closing and getting words. */
  bow_lex* (*open_text_fp) (struct _bow_lexer *self, FILE *fp, 
			    const char *filename);
  bow_lex* (*open_str) (struct _bow_lexer *self, char *buf);
  int (*get_word) (struct _bow_lexer *self, bow_lex *lex, 
		   char *buf, int buflen);
  int (*get_raw_word) (struct _bow_lexer *self, bow_lex *lex, 
		       char *buf, int buflen);
  int (*postprocess_word) (struct _bow_lexer *self, bow_lex *lex,
			   char *buf, int buflen);
  void (*close) (struct _bow_lexer *self, bow_lex *lex);
} bow_lexer;

/* Lexer global variables.  Default values are in lex-simple.c */

/* How to recognize the beginning and end of a document.  NULL pattern
   means don't scan forward at all.  "" means scan forward to EOF. */
extern const char *bow_lexer_document_start_pattern;
extern const char *bow_lexer_document_end_pattern;
extern int *bow_lexer_case_sensitive;
extern int (*bow_lexer_stoplist_func)(const char *);
extern int (*bow_lexer_stem_func)(char *);
extern int bow_lexer_toss_words_longer_than;
extern int bow_lexer_toss_words_shorter_than;
extern char *bow_lexer_infix_separator;
extern int bow_lexer_infix_length;
extern int bow_lexer_max_num_words_per_document;

/* The parameters that control lexing.  Many of these may be changed
   with command-line options. */
typedef struct _bow_lexer_parameters {
  int (*true_to_start)(int character);          /* non-zero on char to start */
  int (*false_to_end)(int character);           /* zero on char to end */
  int strip_non_alphas_from_end;                /* boolean */
  int toss_words_containing_non_alphas;	        /* boolean */
  int toss_words_containing_this_many_digits;
} bow_lexer_parameters;

/* Get the raw token from the document buffer by scanning forward
   until we get a start character, and filling the buffer until we get
   an ending character.  The resulting token in the buffer is
   NULL-terminated.  Return the length of the token. */
int bow_lexer_simple_get_raw_word (bow_lexer *self, bow_lex *lex, 
				   char *buf, int buflen);

/* Perform all the necessary postprocessing after the initial token
   boundaries have been found: strip non-alphas from end, toss words
   containing non-alphas, toss words containing certaing many digits,
   toss words appearing in the stop list, stem the word, check the
   stoplist again, toss words of length one.  If the word is tossed,
   return zero, otherwise return the length of the word. */
int bow_lexer_simple_postprocess_word (bow_lexer *self, bow_lex *lex, 
				       char *buf, int buflen);

/* Create and return a BOW_LEX, filling the document buffer from
   characters in FP, starting after the START_PATTERN, and ending with
   the END_PATTERN. */
bow_lex *bow_lexer_simple_open_text_fp (bow_lexer *self, FILE *fp,
					const char *filename);

/* Create and return a BOW_LEX, filling the document buffer from
   characters in BUF, starting after the START_PATTERN, and ending with
   the END_PATTERN.  NOTE: BUF is not modified, and it does not need to 
   be saved for future use. */
bow_lex *bow_lexer_simple_open_str (bow_lexer *self, char *buf);

/* Close the LEX buffer, freeing the memory held by it. */
void bow_lexer_simple_close (bow_lexer *self, bow_lex *lex);

/* Scan a single token from the LEX buffer, placing it in BUF, and
   returning the length of the token.  BUFLEN is the maximum number of
   characters that will fit in BUF.  If the token won't fit in BUF,
   an error is raised. */
int bow_lexer_simple_get_word (bow_lexer *self, bow_lex *lex, 
			       char *buf, int buflen);


/* Here are some simple, ready-to-use lexers that are implemented in
   lex-simple.c */

extern const bow_lexer *bow_simple_lexer;

/* A lexer that throws out all space-delimited strings that have any
   non-alphabetical characters.  For example, the string `obtained
   from http://www.cs.cmu.edu' will result in the tokens `obtained'
   and `from', but the URL will be skipped. */
extern const bow_lexer_parameters *bow_alpha_only_lexer_parameters;

/* A lexer that keeps all alphabetic strings, delimited by
   non-alphabetic characters.  For example, the string
   `http://www.cs.cmu.edu' will result in the tokens `http', `www',
   `cs', `cmu', `edu'. */
extern const bow_lexer_parameters *bow_alpha_lexer_parameters;

/* A lexer that keeps all alphabetic strings, delimited by
   non-alphabetic characters.  For example, the string
   `http://www.cs.cmu.edu:8080' will result in the tokens `http', `www',
   `cs', `cmu', `edu', `8080'. */
extern const bow_lexer_parameters *bow_alphanum_lexer_parameters;

/* A lexer that keeps all strings that begin and end with alphabetic
   characters, delimited by white-space.  For example,
   the string `http://www.cs.cmu.edu' will be a single token. 
   This does not change the words at all---no down-casing, no stemming,
   no stoplist, no word tossing.  It's ideal for use when a
   --lex-pipe-command is used to do all the tokenizing.  */
extern const bow_lexer_parameters *bow_white_lexer_parameters;

/* A lexer that prepends all tokens by the `Date:' string at the 
   beginning of the line. */
extern const bow_lexer *bow_suffixing_lexer;


/* Call-back functions that just call the next lexer.  */

/* Open using the next lexer. */
bow_lex *bow_lexer_next_open_text_fp (bow_lexer *self, FILE *fp,
				      const char *filename);

/* Open using the next lexer from a string. */
bow_lex *bow_lexer_next_open_str (bow_lexer *self, char *buf);

/* Get a word using the next lexer */
int bow_lexer_next_get_word (bow_lexer *self, bow_lex *lex, 
			     char *buf, int buflen);

/* Get a raw word using the next lexer */
int bow_lexer_next_get_raw_word (bow_lexer *self, bow_lex *lex, 
				 char *buf, int buflen);

/* Postprocess a word using the next lexer */
int bow_lexer_next_postprocess_word (bow_lexer *self, bow_lex *lex, 
				     char *buf, int buflen);

/* Close the underlying lexer. */
void bow_lexer_next_close (bow_lexer *self, bow_lex *lex);


/* Some declarations for a simple N-gram lexer.  See lex-gram.c */

/* An augmented version of BOW_LEXER that provides N-grams */
typedef struct _bow_lexer_gram {
  bow_lexer lexer;
  int gram_size;
} bow_lexer_gram;

/* An augmented version of BOW_LEX that works for N-grams */
typedef struct _bow_lex_gram {
  bow_lex lex;
  int gram_size_this_time;
} bow_lex_gram;

/* A lexer that returns N-gram tokens using BOW_ALPHA_ONLY_LEXER.
   It actually returns all 1-grams, 2-grams ... N-grams, where N is 
   specified by GRAM_SIZE.  */
extern const bow_lexer_gram *bow_gram_lexer;

/* A lexer that ignores all HTML directives, ignoring all characters
   between angled brackets: < and >. */
extern const bow_lexer *bow_html_lexer;

/* An unsorted, NULL-terminated array of strings, indicating headers
   which should be removed from an e-mail/newsgroup message.  If this
   pointer is not NULL, the GET_WORD() function should be
   BOW_LEXER_EMAIL_GET_WORD. */
extern char **bow_email_headers_to_remove;

/* A lexer that removes all header lines which is one of the headers contained
   in HEADERS_TO_REMOVE */
extern const bow_lexer *bow_email_lexer;


/* The default lexer that will be used by various library functions
   like BOW_WV_NEW_FROM_TEXT_FP().  You should set this variable to
   point at whichever lexer you desire.  If you do not set it, it
   will point at bow_alpha_lexer. */
extern bow_lexer *bow_default_lexer;
extern bow_lexer_parameters *bow_default_lexer_parameters;


/* Functions that may be useful in writing a lexer. */

/* Apply the Porter stemming algorithm to modify WORD.  Return 0 on success. */
int bow_stem_porter (char *word);

/* A function wrapper around POSIX's `isalpha' macro. */
int bow_isalpha (int character);

/* A function wrapper around POSIX's `isgraph' macro. */
int bow_isgraph (int character);

/* Return non-zero if WORD is on the stoplist. */
int bow_stoplist_present (const char *word);

/* Return non-zero if WORD is on the stoplist, where HASH corresponds
   to int4str.c:_str2id */
int bow_stoplist_present_hash (const char *word, unsigned hash);

/* Add to the stoplist the white-space delineated words from FILENAME.
   Return the number of words added.  If the file could not be opened,
   return -1. */
int bow_stoplist_add_from_file (const char *filename);

/* Empty the default stoplist, and add space-delimited words from FILENAME. */
void bow_stoplist_replace_with_file (const char *filename);

/* Add WORD to the stop list. */
void bow_stoplist_add_word (const char *word);


/* Arrays of C struct's that can grow. */

typedef struct _bow_array {
  int length;			/* number of elements in the array */
  int size;			/* number of elts for which alloc'ed space */
  int entry_size;		/* number of bytes in each entry */
  void (*free_func)(void*);	/* call this with each entry when freeing */
  int growth_factor;		/* mult, then divide by 1-this when realloc */
  void *entries;		/* the malloc'ed space for the entries */
} bow_array;

extern int bow_array_default_capacity;
extern int bow_array_default_growth_factor;

/* Allocate, initialize and return a new array structure. */
bow_array *bow_array_new (int capacity, int entry_size, void (*free_func)());

/* Initialize an already allocated array structure. */
void bow_array_init (bow_array *array, int capacity, 
		     int entry_size, void (*free_func)());

/* Append an entry to the array.  Return its index. */
int bow_array_append (bow_array *array, void *entry);

/* Append an entry to the array by reading from fp.  Return its index,
   or -1 if there are no more entries to be read. */
int bow_array_append_from_fp_inc (bow_array *array, 
				  int (*read_func)(void*,FILE*), 
				  FILE *fp);

/* Return what will be the index of the next entry to be appended */
int bow_array_next_index (bow_array *array);

/* Return a pointer to the array entry at index INDEX. */
void *bow_array_entry_at_index (bow_array *array, int index);

/* Write the array ARRAY to the file-pointer FP, using the function
   WRITE_FUNC to write each of the entries in ARRAY. */
void bow_array_write (bow_array *array, int (*write_func)(void*,FILE*), 
		      FILE *fp);

/* Write the incremental format header to the file-pointer FP */
void bow_array_write_header_inc (bow_array *array, FILE *fp);

/* Write one entry in incremental format to the file-pointer FP, using the function
   WRITE_FUNC. It will fseek to the appropriate location to write. */
void bow_array_write_entry_inc (bow_array *array, int i, int (*write_func)(void*,FILE*), FILE *fp);

/* Return a new array, created by reading file-pointer FP, and using
   the function READ_FUNC to read each of the array entries. The
   returned array will have entry-freeing-function FREE_FUNC. */
bow_array *
bow_array_new_from_fp_inc (int (*read_func)(void*,FILE*), 
			   void (*free_func)(),
			   FILE *fp);

/* Return a new array, created by reading file-pointer FP, and using
   the function READ_FUNC to read each of the array entries.  The
   returned array will have entry-freeing-function FREE_FUNC. */
bow_array *
bow_array_new_from_data_fp (int (*read_func)(void*,FILE*), 
			    void (*free_func)(),
			    FILE *fp);

/* Return a new array, created by reading file-pointer FP, and using
   the function READ_FUNC to read each of the array entries.  The
   array entries will have size MIN_ENTRY_SIZE, or larger, if
   indicated by the data in FP; this is useful when a structure is
   re-defined to be larger.  The returned array will have
   entry-freeing-function FREE_FUNC.  */
bow_array *
bow_array_new_with_entry_size_from_data_fp (int min_entry_size,
					    int (*read_func)(void*,FILE*), 
					    void (*free_func)(),
					    FILE *fp);

/* Free the memory held by the array ARRAY. */
void bow_array_free (bow_array *array);



/* Sparse arrays of C struct's that can grow. */

typedef struct _bow_sparray {
  void **entry_root;
  int entry_size;		/* number of bytes in each entry */
  void (*free_func)(void*);	/* call this with each entry when freeing */
} bow_sparray;




/* Managing int->string and string->int mappings. */

typedef struct _bow_int4str {
  const char **str_array;
  int str_array_length;
  int str_array_size;
  int *str_hash;
  int str_hash_size;
} bow_int4str;

/* Allocate, initialize and return a new int/string mapping structure.
   The parameter CAPACITY is used as a hint about the number of words
   to expect; if you don't know or don't care about a CAPACITY value,
   pass 0, and a default value will be used. */
bow_int4str *bow_int4str_new (int capacity);

/* Given a integer INDEX, return its corresponding string. */
const char *bow_int2str (bow_int4str *map, int index);

/* Given the char-pointer STRING, return its integer index.  If this is 
   the first time we're seeing STRING, add it to the mapping, assign
   it a new index, and return the new index. */
int bow_str2int (bow_int4str *map, const char *string);

/* Just like BOW_STR2INT, except assume that the STRING's ID has
   already been calculated. */
int _bow_str2int (bow_int4str *map, const char *string, unsigned id);

/* Given the char-pointer STRING, return its integer index.  If STRING
   is not yet in the mapping, return -1. */
int bow_str2int_no_add (bow_int4str *map, const char *string);

/* Create a new int-str mapping by lexing words from FILE. */
bow_int4str *bow_int4str_new_from_text_file (const char *filename);

/* Create a new int-str mapping words fscanf'ed from FILE using %s. */
bow_int4str *bow_int4str_new_from_string_file (const char *filename);

/* Write the int-str mapping to file-pointer FP. */
void bow_int4str_write (bow_int4str *map, FILE *fp);

/* Return a new int-str mapping, created by reading file-pointer FP. */
bow_int4str *bow_int4str_new_from_fp (FILE *fp);

/* Same as above, but in incremental format. */
bow_int4str *bow_int4str_new_from_fp_inc (FILE *fp);

/* Return a new int-str mapping, created by reading FILENAME. */
bow_int4str *bow_int4str_new_from_file (const char *filename);

/* Free the memory held by the int-word mapping MAP. */
void bow_int4str_free (bow_int4str *map);



/* Arrays of C struct's that can grow.  Entries can be retrieved
   either by integer index, or by string key. */

typedef struct _bow_sarray {
  bow_array *array;
  bow_int4str *i4k;
} bow_sarray;

extern int bow_sarray_default_capacity;

/* Allocate, initialize and return a new sarray structure. */
bow_sarray *bow_sarray_new (int capacity, int entry_size, void (*free_func)());

/* Initialize a newly allocated sarray structure. */
void bow_sarray_init (bow_sarray *sa, int capacity,
		      int entry_size, void (*free_func)());

/* Append a new entry to the array.  Also make the entry accessible by
   the string KEYSTR.  Returns the index of the new entry. */
int bow_sarray_add_entry_with_keystr (bow_sarray *sa, void *entry,
				      const char *keystr);

/* Append a new entry to the array.  Also make the entry accessible by
   the string KEYSTR. Reflect changes on disk.
   Returns the index of the new entry. */
int
bow_sarray_add_entry_with_keystr_inc (bow_sarray *sa, void *entry,
				      const char *keystr, int (*write_func)(void*,FILE*),
				      FILE *i4k_fp, FILE *array_fp);

/* Return a pointer to the entry at index INDEX. */
void *bow_sarray_entry_at_index (bow_sarray *sa, int index);

/* Return a pointer to the entry associated with string KEYSTR. */
void *bow_sarray_entry_at_keystr (bow_sarray *sa, const char *keystr);

/* Return the string KEYSTR associated with the entry at index INDEX. */
const char *bow_sarray_keystr_at_index (bow_sarray *sa, int index);

/* Return the index of the entry associated with the string KEYSTR. */
int bow_sarray_index_at_keystr (bow_sarray *sa, const char *keystr);

/* Write the sarray SARRAY to the file-pointer FP, using the function
   WRITE_FUNC to write each of the entries in SARRAY. */
void bow_sarray_write (bow_sarray *sarray, int (*write_func)(void*,FILE*), 
		       FILE *fp);

/* Return a new sarray, created by reading file-pointer FP, and using
   the function READ_FUNC to read each of the sarray entries.  The
   returned sarray will have entry-freeing-function FREE_FUNC. */
bow_sarray *bow_sarray_new_from_data_fp (int (*read_func)(void*,FILE*), 
					 void (*free_func)(),
					 FILE *fp);

/* Return a new sarray, created by reading file-pointers I4K_FP and ARRAY_FP, and using
   the function READ_FUNC to read each of the icremental-format array entries from
   FP_ARRAY.  The returned sarray will have entry-freeing-function FREE_FUNC. */
bow_sarray *
bow_sarray_new_from_data_fps_inc (int (*read_func)(void*,FILE*), 
				  void (*free_func)(),
				  FILE *i4k_fp, FILE *array_fp);

/* Free the memory held by the bow_sarray SA. */
void bow_sarray_free (bow_sarray *sa);



/* Bit vectors, indexed by multiple dimensions.  They can grow
   automatically in the last dimension. */

typedef struct _bow_bitvec {
  int num_dimensions;		/* the number of dimensions by which indexed */
  int *dimension_sizes;		/* the sizes of each index dimension */
  int vector_size;		/* size of VECTOR in bytes */
  int bits_set;                 /* number of bits set to 1 */
  unsigned char *vector;	/* the memory for the bit vector */
} bow_bitvec;

/* Allocate, initialize and return a new "bit vector" that is indexed
   by NUM_DIMENSIONS different dimensions.  The size of each dimension
   is given in DIMENSION_SIZES.  The size of the last dimension is
   used as hint for allocating initial memory for the vector, but in
   practice, higher indices for the last dimension can be used later,
   and the bit vector will grow automatically.  Initially, the bit
   vector contains all zeros. */
bow_bitvec *bow_bitvec_new (int num_dimensions, int *dimension_sizes);

/* Set all the bits in the bit vector BV to 0 if value is zero, to 1
   otherwise. */
void bow_bitvec_set_all_to_value (bow_bitvec *bv, int value);

/* If VALUE is non-zero, set the bit at indices INDICES to 1,
   otherwise set it to zero.  Returns the previous value of that
   bit. */
int bow_bitvec_set (bow_bitvec *bv, int *indices, int value);

/* Return the value of the bit at indicies INDICIES. */
int bow_bitvec_value (bow_bitvec *bv, int *indices);

/* Free the memory held by the "bit vector" BV. */
void bow_bitvec_free (bow_bitvec *bv);



/* A trie for testing membership in a set of lowercase alphabetic strings */
typedef struct _bow_strtrie {
  struct _bow_strtrie *next[27];
} bow_strtrie;

/* Return a new strtrie */
bow_strtrie *bow_strtrie_new ();

/* Add the string STR to the trie STRIE */
void bow_strtrie_add (bow_strtrie *strie, const char *str);

/* Return non-zero if the string STR is present in the trie STRIE */
int bow_strtrie_present (bow_strtrie *strie, const char *str);

/* Free the memory occupied by STRIE */
void bow_strtrie_free (bow_strtrie *strie);



/* A convient interface to a specific instance of the above int/string
   mapping; this one is intended for all the words encountered in all
   documents. */

/* Given a "word index" WI, return its WORD, according to the global
   word-int mapping. */
const char *bow_int2word (int wi);

/* Given a WORD, return its "word index", WI, according to the global
   word-int mapping; if it's not yet in the mapping, add it. */
int bow_word2int (const char *word);

/* Given a WORD, return its "word index", WI, according to the global
   word-int mapping; if it's not yet in the mapping, add it, and
   write the word to file-pointer fp as well. */
int bow_word2int_inc (const char *word, FILE *fp);

/* Given a WORD, return its "word index", WI, according to the global
   word-int mapping; if it's not yet in the mapping, return -1. */
int bow_word2int_no_add (const char *word);

/* Like bow_word2int(), except it also increments the occurrence count 
   associated with WORD. */
int bow_word2int_add_occurrence (const char *word);

/* The int/string mapping for bow's vocabulary words. */
extern bow_int4str *word_map;

/* If this is non-zero, then bow_word2int() will return -1 when asked
   for the index of a word that is not already in the mapping.
   Essentially, setting this to non-zero makes bow_word2int() and
   bow_word2int_add_occurrence() behave like bow_str2int_no_add(). */
extern int bow_word2int_do_not_add;

/* If this is non-zero and bow_word2int_do_not_add is non-zero, then
   bow_word2int() will return the index of the "<unknown>" token when
   asked for the index of a word that is not already in the mapping. */
extern int bow_word2int_use_unknown_word;

#define BOW_UNKNOWN_WORD "<unknown>"

/* Add to the word occurrence counts from the documents in FILENAME. */
int bow_words_add_occurrences_from_file (const char *filename);

/* Add to the word occurrence counts by recursively decending directory 
   DIRNAME and lexing all the text files; skip any files matching
   EXCEPTION_NAME. */
int bow_words_add_occurrences_from_text_dir (const char *dirname,
					     const char *exception_name);

/* Add to the word occurrence counts reading all entries in HDB
   database DIRNAME and parsing all the text files; skip any files
   matching EXCEPTION_NAME. */
int bow_words_add_occurrences_from_hdb (const char *dirname,
					const char *exception_name);

/* Return the number of times bow_word2int_add_occurrence() was
   called with the word whose index is WI. */
int bow_words_occurrences_for_wi (int wi);

/* Replace the current word/int mapping with MAP. */
void bow_words_set_map (bow_int4str *map, int free_old_map);

/* Modify the int/word mapping by removing all words that occurred 
   less than OCCUR number of times.  WARNING: This totally changes
   the word/int mapping; any WV's, WI2DVF's or BARREL's you build
   with the old mapping will have bogus WI's afterward. */
void bow_words_remove_occurrences_less_than (int occur);

/* Return the total number of unique words in the int/word map. */
int bow_num_words ();

/* Save the int/word map to file-pointer FP. */
void bow_words_write (FILE *fp);

/* Same as above, but with a filename instead of a FILE* */
void bow_words_write_to_file (const char *filename);

/* Read the int/word map from file-pointer FP. */
void bow_words_read_from_fp (FILE *fp);

/* Same as above, but with a filename instead of a FILE* */
void bow_words_read_from_file (const char *filename);

/* Same as above, but don't bother rereading unless filename is different
   from the last one, or FORCE_UPDATE is non-zero. */
void bow_words_reread_from_file (const char *filename, int force_update);

/* Read the int/word map (in incremental format) from file-pointer FP. */
void bow_words_read_from_fp_inc (FILE *fp);


/* Lists of words sorted by some score, for example, infogain */

/* An entry for a word and its score */
typedef struct _bow_ws {
  int wi;
  float weight;
} bow_ws;

/* An array of words and their scores. */
typedef struct _bow_wa {
  int size;
  int length;
  bow_ws *entry;
} bow_wa;

/* Create a new, empty array of word/score entries, with CAPACITY entries. */
bow_wa *bow_wa_new (int capacity);

/* Add a new word and score to the array */
int bow_wa_append (bow_wa *wa, int wi, float score);

/* Add a score to the array.  If there is already an entry for WI, the
   SCORE gets added to WI's current score.  If WI is not already in
   the array, then this function behaves like bow_wa_append(). */
int bow_wa_add (bow_wa *wa, int wi, float score);

/* Add a score to the array.  If there is already an entry for WI at
   the end, the SCORE gets added to WI's current score.  If WI is
   greater than the WI at the end, then this function behaves like
   bow_wa_append(), otherwise an error is raised. */
int bow_wa_add_to_end (bow_wa *wa, int wi, float score);

/* Remove the entry corresponding to word WI.  Return the new length
   of the word array. */
int bow_wa_remove (bow_wa *wa, int wi);

/* Add to WA all the WI/WEIGHT entries from WA2.  Uses bow_wa_add(). */
int bow_wa_union (bow_wa *wa, bow_wa *wa2);

/* Return a new array containing only WI entries that are in both 
   WA1 and WA2. */
bow_wa *bow_wa_intersection (bow_wa *wa1, bow_wa *wa2);

/* Add weights to WA1 for those entries appearing in WA2 */
int bow_wa_overlay (bow_wa *wa1, bow_wa *wa2);

/* Return a new array containing only WI entries that are in WA1 but
   not in WA2. */
bow_wa *bow_wa_diff (bow_wa *wa1, bow_wa *wa2);

/* Sort the word array with high values first. */
void bow_wa_sort (bow_wa *wa);
/* Sort the word array with high values last. */
void bow_wa_sort_reverse (bow_wa *wa);

/* Print the first N entries of the word array WA to stream FP. */
void bow_wa_fprintf (bow_wa *wa, FILE *fp, int n);

/* Remove all entries from the word array */
void bow_wa_empty (bow_wa *wa);

/* Free the word array */
void bow_wa_free (bow_wa *wa);


/* Word vectors.  A "word vector" is sorted array of words, with count
   information attached to each word.  Typically, there would be one
   "word vector" associated with a document, or with a concept. */

/* A "word entry"; these are the elements of a "word vector" */
typedef struct _bow_we {
  int wi;
  int count;
  float weight;
} bow_we;

/* A "word vector", containing an array of words with their statistics */
typedef struct _bow_wv {
  int num_entries;		/* the number of unique words in the vector */
  float normalizer;		/* multiply weights by this for normalizing */
  bow_we entry[0];
} bow_wv;

/* Create and return a new "word vector" from a file. */
bow_wv *bow_wv_new_from_text_fp (FILE *fp, const char *filename);

/* Create and return a new "word vector" from a string. */
bow_wv *bow_wv_new_from_text_string (char *the_string);

/* Create and return a new "word vector" from a document buffer LEX. */
bow_wv *bow_wv_new_from_lex (bow_lex *lex);

/* Create and return a new "word vector" that is the sum of all the
   "word vectors" in WV_ARRAY.  The second parameter, WV_ARRAY_LENGTH,
   is the number of "word vectors" in WV_ARRAY. */
bow_wv *bow_wv_add (bow_wv **wv_array, int wv_array_length);

/* Create and return a new "word vector" with uninitialized contents. */
bow_wv *bow_wv_new (int capacity);

/* Allocate a return a copy of WV */
bow_wv * bow_wv_copy (bow_wv *wv);

/* Return the number of word occurrences in the WV */
int bow_wv_word_count (bow_wv *wv);

/* Return a pointer to the "word entry" with index WI in "word vector WV */
bow_we *bow_wv_entry_for_wi (bow_wv *wv, int wi);

/* Return the count entry of "word" with index WI in "word vector" WV */
int bow_wv_count_for_wi (bow_wv *wv, int wi);

/* Print "word vector" WV on stream FP in a human-readable format. */
void bow_wv_fprintf (FILE *fp, bow_wv *wv);

/* Print "word vector" WV to a string in a human-readable format. */
char *bow_wv_sprintf (bow_wv *wv, unsigned int max_size_for_string);

/* Print "word vector"'s actual words to a string. */
char *bow_wv_sprintf_words (bow_wv *wv, unsigned int max_size_for_string);

/* Assign a value to the "word vector's" NORMALIZER field, such that
   when all the weights in the vector are multiplied by the
   NORMALIZER, the Euclidian length of the vector will be one. */
void bow_wv_normalize_weights_by_vector_length (bow_wv *wv);

/* Assign a value to the "word vector's" NORMALIZER field, such that
   when all the weights in the vector are multiplied by the
   NORMALIZER, all the vector entries will to one. */
void bow_wv_normalize_weights_by_summing (bow_wv *wv);

/* Return the sum of the weight entries. */
float bow_wv_weight_sum (bow_wv *wv);

/* Return the number of bytes required for writing the "word vector" WV. */
int bow_wv_write_size (bow_wv *wv);

/* Write "word vector" DV to the stream FP. */
void bow_wv_write (bow_wv *wv, FILE *fp);

/* Return a new "word vector" read from a pointer into a data file, FP. */
bow_wv *bow_wv_new_from_data_fp (FILE *fp);

/* Free the memory held by the "word vector" WV. */
void bow_wv_free (bow_wv *wv);

/* Collections of "word vectors. */

/* An array that maps "document indices" to "word vectors" */
typedef struct _bow_di2wv {
  int length;
  int size;
  bow_wv *entry[0];
} bow_di2wv;



/* Documents */  

/* We want a nice way of saying this is a training or test document, or do
   we ignore it for now. */
typedef enum {
  bow_doc_train,	/* Use this to calculate P(w|C) */
  bow_doc_test,		/* Classify these for test results */
  bow_doc_unlabeled,    /* the "unlabeled" docs in EM and active learning */
  bow_doc_untagged,	/* Not yet assigned a tag */
  bow_doc_validation,   /* docs used for a validation set */
  bow_doc_ignore,	/* docs left unused */
  bow_doc_pool,         /* the cotraining candidate pool */
  bow_doc_waiting       /* the "unlabeled" docs not used by cotraining yet */
} bow_doc_type;

#define bow_str2type(STR) \
((strcmp (STR, "train") == 0) \
 ? bow_doc_train \
 : ((strcmp (STR, "test") == 0) \
    ? bow_doc_test \
    : ((strcmp (STR, "unlabeled") == 0) \
       ? bow_doc_unlabeled \
       : ((strcmp (STR, "validation") == 0) \
	  ? bow_doc_validation \
	  : ((strcmp (STR, "ignore") == 0) \
	     ? bow_doc_ignore \
	     : ((strcmp (STR, "pool") == 0) \
		? bow_doc_pool \
		: ((strcmp (STR, "waiting") == 0) \
		   ? bow_doc_waiting \
		   : -1)))))))
     
#define bow_type2str(T) \
((T == bow_doc_train)                                \
 ? "train"                                          \
 : ((T == bow_doc_test)                             \
    ? "test"                                        \
    : ((T == bow_doc_unlabeled)                     \
       ? "unlabeled"                                \
       : ((T == bow_doc_untagged)                   \
	  ? "untagged"                              \
	  : ((T == bow_doc_validation)              \
	     ? "validation"                         \
	     : ((T == bow_doc_ignore)               \
		? "ignore"                          \
		: ((T == bow_doc_pool)            \
		   ? "pool"                         \
		   : ((T == bow_doc_waiting)        \
		      ? "waiting"                  \
		      : "UNKNOWN DOC TYPE"))))))))


/* A generic "document" entry, useful for setting document types.  
   All other "document" entries should begin the same as this one. */
typedef struct _bow_doc {
  bow_doc_type type;
  int class;
  const char *filename;
} bow_doc;

/* These are defined in split.c */
int bow_doc_is_train (bow_doc *doc);
int bow_doc_is_test (bow_doc *doc);
int bow_doc_is_unlabeled (bow_doc *doc);
int bow_doc_is_untagged (bow_doc *doc);
int bow_doc_is_validation (bow_doc *doc);
int bow_doc_is_ignore (bow_doc *doc);
int bow_doc_is_pool (bow_doc *doc);
int bow_doc_is_waiting (bow_doc *doc);


/* A "document" entry useful for standard classification tasks. */
typedef struct _bow_cdoc {
  bow_doc_type type;		/* Is this document part of the model to be
				   built, a test document, or to be ignored */
  int class;			/* A classification label. */
  const char *filename;		/* Where to find the original document */
  int word_count;		/* Total number of words in this document */
  float normalizer;		/* Multiply weights by this for normalizing */
  float prior;			/* Prior probability of this class/doc */
  float *class_probs;           /* Probabilistic classification labels */
} bow_cdoc;

/* A convenient interface to bow_array that is specific to bow_cdoc. */
#define bow_cdocs bow_array
#define bow_cdocs_new(CAPACITY) bow_array_new (CAPACITY, sizeof (bow_cdoc), 0)
#define bow_cdocs_register_doc(CDOCS,CDOC) bow_array_append (CDOCS, CDOC)
#define bow_cdocs_di2doc(CDOCS, DI) bow_array_entry_at_index (CDOCS, DI)


/* Traversing directories to get filenames. */

/* A list of document names. */
/* xxx We might change this someday to allow for multiple documents
   per file, e.g. for "mbox" files containing many email messages. */
typedef struct _bow_doc_list {
  struct _bow_doc_list *next;
  char filename[0];
} bow_doc_list;

/* Return a non-zero value if the file FP contains mostly text. */
int bow_fp_is_text (FILE *fp);

/* Return a non-zero value if the char array BUF contains mostly text. */
int bow_str_is_text (char *buf);

/* bow_*_is_text() always returns `yes'.  This is useful for Japanese
   byte codes. */
extern int bow_is_text_always_yes;

/* Calls the function CALLBACK for each of the filenames encountered
   when recursively descending the directory named DIRNAME.  CALLBACK
   should be a pointer to function that takes a filename char-pointer,
   and a void-pointer as arguments and returns an integer.  Currently
   the return value is ignored, but it may be used in the future to
   cut short, causing bow_map_filesnames_from_dir to return
   immediately.  The value CONTEXT will be passed as the second
   argument to the CALLBACK function; it provides you with a way to
   transfer context you may need inside the implementation of the
   callback function.  EXCLUDE_PATTERNS is currently ignored. */
int
bow_map_filenames_from_dir (int (*callback)(const char *filename, 
					    void *context),
			    void *context,
			    const char *dirname,
			    const char *exclude_patterns);

/* Calls the function CALLBACK for each of the files found in the
   database DIRNAME_ARG.  See bow_map_filenames_from_dir for more info */
int
bow_map_filenames_from_hdb (int (*callback)(const char *filename, char *data,
					    void *context),
			    void *context,
			    const char *dirname,
			    const char *exclude_patterns);

/* Create a linked list of filenames, and append the file list pointed
   to by FL to it; return the new concatenated lists in *FL.  The
   function returns the total number of filenames.  When creating the
   list, look for files (and recursively descend directories) among
   those matching INCLUDE_PATTERNS, but don't include those matching
   EXCLUDE_PATTERNS; don't include files that aren't text files. */
/* xxx For now, this only works with a single directory name in
   INCLUDE_PATTERNS, and it ignores EXCLUDE_PATTERNS. */
int bow_doc_list_append (bow_doc_list **list, 
			 const char *include_patterns,
			 const char *exclude_patterns);

/* Print the file list FL to the output stream FP. */
void bow_doc_list_fprintf (FILE *fp, bow_doc_list *fl);

/* Return the number of entries in the "docname list" DL. */
int bow_doc_list_length (bow_doc_list *dl);

/* Free the memory held by the file list FL. */
void bow_doc_list_free (bow_doc_list *fl);



/* A convient interface to a specific instance of the above int/string
   mapping; this one is intended for all the documents encountered. */

/* Given a "word index" WI, return its WORD, according to the global
   word-int mapping. */
const char *bow_int2docname (int wi);

/* Given a WORD, return its "word index", WI, according to the global
   word-int mapping; if it's not yet in the mapping, add it. */
int bow_docname2int (const char *word);

/* Return the total number of unique words in the int/word map. */
int bow_num_docnames ();

/* Save the docname map to file-pointer FP. */
void bow_docnames_write (FILE *fp);

/* Read the docname from file-pointer FP. */
void bow_docnames_read_from_fp (FILE *fp);



/* xxx Perhaps the name should be changed from "dv" to "cv", for
   "class vector", or "concept vector", or "codebook vector". */
/* Document vectors.  A "document vector" is a sorted array of
   documents, with count information attached to each document.
   Typically, there would be one "document vector" associated with a
   word.  If "word vectors" are the rows of a large matrix, "document
   vectors" are the columns.  It can be more efficient to search just
   the docment vectors of the words in the query document, than it is
   to search the word vectors of all documents. */

/* A "document entry"; these are the elements of a "document vector". */
typedef struct _bow_de {
  int di;			/* a "document index" */
  int count;			/* number of times X appears in document DI */
  float weight;
} bow_de;

/* A "document vector" */ 
typedef struct _bow_dv {
  int length;			/* xxx Rename this to num_entries */
  int size;
  float idf;                    /* The idf factor for this word. */
  bow_de entry[0];
} bow_dv;

/* Create a new, empty "document vector". */
bow_dv *bow_dv_new (int capacity);

/* The default capacity used when 0 is passed for CAPACITY above. */
extern unsigned int bow_dv_default_capacity; 

/* Add a new entry to the "document vector" *DV. */
void bow_dv_add_di_count_weight (bow_dv **dv, int di, int count, float weight);

/* Set the count & weight of the "document vector" *DV. */
void bow_dv_set_di_count_weight (bow_dv **dv, int di, int count, float weight);

/* Sum the WEIGHT into the document vector DV at document index DI,
   creating a new entry in the document vector if necessary. */
void bow_dv_add_di_weight (bow_dv **dv, int di, float weight);

/* Return a pointer to the BOW_DE for a particular document, or return
   NULL if there is no entry for that document. */
bow_de *bow_dv_entry_at_di (bow_dv *dv, int di);

/* Write "document vector" DV to the stream FP. */
void bow_dv_write (bow_dv *dv, FILE *fp);

/* Return the number of bytes required for writing the "document vector" DV. */
int bow_dv_write_size (bow_dv *dv);

/* Return a new "document vector" read from a pointer into a data file, FP. */
bow_dv *bow_dv_new_from_data_fp (FILE *fp);

/* Free the memory held by the "document vector" DV. */
void bow_dv_free (bow_dv *dv);

/* A "document vector with file info (file storage information)" */
typedef struct _bow_dvf {
  int seek_start;
  bow_dv *dv;
} bow_dvf;


/* xxx Perhaps these should be generalized and renamed to `bow_i2v'? */
/* An array that maps "word indices" to "document vectors with file info" */
typedef struct _bow_wi2dvf {
  int size;			/* the number of ENTRY's allocated */
  int num_words;		/* number of non-NULL dv's in this wi2dvf */
  FILE *fp;			/* where to get DVF's that aren't cached yet */
  bow_dvf entry[0];		/* array of info about each word */
} bow_wi2dvf;

/* Create an empty `wi2dvf' */
bow_wi2dvf *bow_wi2dvf_new (int capacity);

/* The default capacity used when 0 is passed for CAPACITY above. */
extern unsigned int bow_wi2dvf_default_capacity;

/* Create a `wi2dvf' by reading data from file-pointer FP.  This
   doesn't actually read in all the "document vectors"; it only reads
   in the DVF information, and lazily loads the actual "document
   vectors". */
bow_wi2dvf *bow_wi2dvf_new_from_data_fp (FILE *fp);

/* Create a `wi2dvf' by reading data from a file.  This doesn't actually 
   read in all the "document vectors"; it only reads in the DVF 
   information, and lazily loads the actually "document vectors". */
bow_wi2dvf *bow_wi2dvf_new_from_data_file (const char *filename);

/* Return the "document vector" corresponding to "word index" WI.  If
   is hasn't been read already, this function will read the "document
   vector" out of the file passed to bow_wi2dvf_new_from_data_file().
   If the DV has been "hidden" (by feature selection, for example) it
   will return NULL.*/
bow_dv *bow_wi2dvf_dv (bow_wi2dvf *wi2dvf, int wi);

/* Return the "document vector" corresponding to "word index" WI.  This
   function will read the "document vector" out of the file passed to
   bow_wi2dvf_new_from_file() if is hasn't been read already.  If the 
   DV has been "hidden" (by feature selection, for example) it will not
   be returned unless EVEN_IF_HIDDEN is non-zero. */
bow_dv *bow_wi2dvf_dv_hidden (bow_wi2dvf *wi2dvf, int wi, int even_if_hidden);

/* Return a pointer to the BOW_DE for a particular word/document pair, 
   or return NULL if there is no entry for that pair. */
bow_de *bow_wi2dvf_entry_at_wi_di (bow_wi2dvf *wi2dvf, int wi, int di);

/* Read all the words from file pointer FP, and add them to the map
   WI2DVF, such that they are associated with document index DI. */
int bow_wi2dvf_add_di_text_fp (bow_wi2dvf **wi2dvf, int di, FILE *fp,
			       const char *filename);

/* Read all the words from file pointer FP, and add them to the map
   WI2DVF, such that they are associated with document index DI. */
int bow_wi2dvf_add_di_text_str (bow_wi2dvf **wi2dvf, int di, char *data,
				const char *filename);

/* Add a "word vector" WV, associated with "document index" DI, to 
   the map WI2DVF. */ 
void bow_wi2dvf_add_di_wv (bow_wi2dvf **wi2dvf, int di, bow_wv *wv);

/* Write WI2DVF to file-pointer FP, in a machine-independent format.
   This is the format expected by bow_wi2dvf_new_from_fp(). */
void bow_wi2dvf_add_wi_di_count_weight (bow_wi2dvf **wi2dvf, int wi,
					int di, int count, float weight);

/* set the count and weight of the appropriate entry in the wi2dvf */
void bow_wi2dvf_set_wi_di_count_weight (bow_wi2dvf **wi2dvf, int wi,
					int di, int count, float weight);

/* Remove the word with index WI from the vocabulary of the map WI2DVF */
void bow_wi2dvf_remove_wi (bow_wi2dvf *wi2dvf, int wi);

/* Temporarily hide the word with index WI from the vocabulary of the
   map WI2DVF. The function BOW_WI2DVF_DV() will no longer see the entry
   for this WI, but */
void bow_wi2dvf_hide_wi (bow_wi2dvf *wi2dvf, int wi);

/* hide all the words that exist */
void bow_wi2dvf_hide_all_wi (bow_wi2dvf *wi2dvf);

/* unhide a specific word index */
void bow_wi2dvf_unhide_wi (bow_wi2dvf *wi2dvf, int wi);

/* Hide all words occuring in only COUNT or fewer number of documents.
   Return the number of words hidden. */
int bow_wi2dvf_hide_words_by_doc_count (bow_wi2dvf *wi2dvf, int count);

/* Hide all words occuring in only COUNT or fewer times.
   Return the number of words hidden. */
int bow_wi2dvf_hide_words_by_occur_count (bow_wi2dvf *wi2dvf, int count);

/* hide all words where the prefix of the word matches the given
   prefix */
int bow_wi2dvf_hide_words_with_prefix (bow_wi2dvf *wi2dvf, char *prefix);

/* hide all words where the prefix of the word doesn't match the given
   prefix */
int bow_wi2dvf_hide_words_without_prefix (bow_wi2dvf *wi2dvf, char *prefix);

/* Make visible all DVF's that were hidden with BOW_WI2DVF_HIDE_WI(). */
void bow_wi2dvf_unhide_all_wi (bow_wi2dvf *wi2dvf);

/* Set the WI2DVF->ENTRY[WI].IDF to the sum of the COUNTS for the
   given WI. */
void bow_wi2dvf_set_idf_to_count (bow_wi2dvf *wi2dvf);

/* Write WI2DVF to file-pointer FP, in a machine-independent format.
   This is the format expected by bow_wi2dvf_new_from_fp(). */
void bow_wi2dvf_write (bow_wi2dvf *wi2dvf, FILE *fp);

/* Write WI2DVF to a file, in a machine-independent format.  This
   is the format expected by bow_wi2dvf_new_from_file(). */
void bow_wi2dvf_write_data_file (bow_wi2dvf *wi2dvf, const char *filename);

/* Compare two maps, and return 0 if they are equal.  This function was
   written for debugging. */
int bow_wi2dvf_compare (bow_wi2dvf *map1, bow_wi2dvf *map2);

/* Print statistics about the WI2DVF map to STDOUT. */
void bow_wi2dvf_print_stats (bow_wi2dvf *map);

/* Free the memory held by the map WI2DVF. */
void bow_wi2dvf_free (bow_wi2dvf *wi2dvf);

/* Remove words that don't occur in WI2DVF */
void bow_wv_prune_words_not_in_wi2dvf (bow_wv *wv, bow_wi2dvf *wi2dvf);

/* xxx Move these to prind.c */

/* If this is non-zero, use uniform class priors. */
extern int bow_prind_uniform_priors;

/* If this is non-zero, scale PrInd P(w|d) by information gain */
extern int bow_prind_scale_by_infogain;

/* If this is zero, do not normalize the PrInd classification scores. */
extern int bow_prind_normalize_scores;

typedef enum {
  bow_smoothing_goodturing,
  bow_smoothing_laplace,
  bow_smoothing_mestimate,
  bow_smoothing_wittenbell,
  bow_smoothing_dirichlet
} bow_smoothing;

/* A wrapper around a wi2dvf/cdocs combination. */
typedef struct _bow_barrel {
  struct _rainbow_method *method; /* TFIDF, NaiveBayes, PrInd, others. */
  bow_array *cdocs;		/* The documents (or classes, for VPC) */
  bow_wi2dvf *wi2dvf;		/* The matrix of words vs documents */
  bow_int4str *classnames;	/* A map between classnames and indices */
  int is_vpc;			/* non-zero if each `document' is a `class' */
} bow_barrel;

/* An array of these is filled in by the method's scoring function. */
typedef struct _bow_score {
  int di;			/* The "document index" for this document */
  double weight;		/* Its score */
  const char *name;
} bow_score;

typedef struct _bow_method {
  /* String identifer for the method, used for selection. */
  const char *name;
} bow_method;

/* The parameters of weighting and scoring in barrel's. */
typedef struct _rainbow_method {
  /* String identifer for the method, used for selection. */
  const char *name;
  /* Functions for implementing parts of the method. */
  void (*set_weights)(bow_barrel *barrel);
  void (*scale_weights)(bow_barrel *barrel, bow_barrel *doc_barrel);
  void (*normalize_weights)(bow_barrel *barrel);
  bow_barrel* (*vpc_with_weights)(bow_barrel *doc_barrel);
  void (*vpc_set_priors)(bow_barrel *barrel, bow_barrel *doc_barrel);
  int (*score)(bow_barrel *barrel, bow_wv *query_wv, 
	       bow_score *scores, int num_scores, int loo_class);
  void (*wv_set_weights)(bow_wv *wv, bow_barrel *barrel);
  void (*wv_normalize_weights)(bow_wv *wv);
  void (*free_barrel)(bow_barrel *barrel);
  /* Parameters of the method. */
  void *params;
} rainbow_method;

/* Macros that make it easier to call the RAINBOW_METHOD functions */

#define bow_barrel_set_weights(BARREL)		\
if ((*(BARREL)->method->set_weights))           \
  ((*(BARREL)->method->set_weights)(BARREL))

#define bow_barrel_scale_weights(BARREL, DOC_BARREL)		\
if ((*(BARREL)->method->scale_weights))				\
  ((*(BARREL)->method->scale_weights)(BARREL, DOC_BARREL))

#define bow_barrel_normalize_weights(BARREL)		\
if ((*(BARREL)->method->normalize_weights))		\
  ((*(BARREL)->method->normalize_weights)(BARREL))

#define bow_barrel_new_vpc_with_weights(BARREL) \
((*(BARREL)->method->vpc_with_weights)(BARREL))

#define bow_barrel_score(BARREL, QUERY_WV, SCORES, NUM_SCORES, LOO_CLASS) \
((*(BARREL)->method->score)(BARREL, QUERY_WV, SCORES, NUM_SCORES, LOO_CLASS))

#define bow_wv_set_weights(WV,BARREL)		\
if ((*(BARREL)->method->wv_set_weights))	\
  ((*(BARREL)->method->wv_set_weights)(WV, BARREL))

#define bow_wv_normalize_weights(WV,BARREL)		\
if (((*(BARREL)->method->wv_normalize_weights)))	\
  ((*(BARREL)->method->wv_normalize_weights)(WV))

#define bow_free_barrel(BARREL)			\
((*(BARREL)->method->free_barrel)(BARREL))

#define bow_barrel_num_classes(BARREL)		\
(((BARREL)->classnames)				\
 ? ((BARREL)->classnames->str_array_length)	\
 : ((BARREL)->cdocs->length))

#define bow_barrel_classname_at_index(BARREL, INDEX) \
(bow_int2str ((BARREL)->classnames, INDEX))

#define bow_barrel_add_classname(BARREL, NAME) \
(bow_str2int ((BARREL)->classnames, NAME))

#include <bow/tfidf.h>
#include <bow/naivebayes.h>
#include <bow/prind.h>
#include <bow/kl.h>
#include <bow/em.h>
#include <bow/knn.h>
// struct argp_child;		/* forward declare this type */

/* Associate method M with the string NAME, so the method structure
   can be retrieved later with BOW_METHOD_AT_NAME().  Set the group
   number of the CHILD so the command-line options for this option
   will appear separately.  If there is no argp_child for this 
   method, pass NULL for CHILD. */
int bow_method_register_with_name (bow_method *m, const char *name, int size,
				   struct argp_child *child);

/* Return a pointer to a method structure that was previous registered 
   with string NAME using BOW_METHOD_REGISTER_WITH_NAME(). */
bow_method *bow_method_at_name (const char *name);

/* The mapping from names to BOW_METHOD's. */
extern bow_sarray *bow_methods;

#define bow_default_method_name "naivebayes"


/* Create a new, empty `bow_barrel', with cdoc's of size ENTRY_SIZE
   and cdoc free function FREE_FUNC.  WORD_CAPACITY and CLASS_CAPACITY
   are just hints. */
bow_barrel *bow_barrel_new (int word_capacity, 
			    int class_capacity,
			    int entry_size, void (*free_func)());

/* Create a new barrel and fill it from contents in --print-barrel=FORMAT
   read in from FILENAME. */
bow_barrel *bow_barrel_new_from_printed_barrel_file (const char *filename,
						     const char *format);

/* Add statistics to the barrel BARREL by indexing all the documents
   found when recursively decending directory DIRNAME.  Return the number
   of additional documents indexed. */
int bow_barrel_add_from_text_dir (bow_barrel *barrel,
				  const char *dirname, 
				  const char *except_name, 
				  const char *classnames);

/* Add statistics to the barrel BARREL by indexing all the documents
   in HDB database DIRNAME.  Return the number of additional
   documents indexed. */
int bow_barrel_add_from_hdb (bow_barrel *barrel,
			     const char *dirname, 
			     const char *except_name, 
			     const char *classnames);

/* Add statistics about the document described by CDOC and WV to the
   BARREL. */
int bow_barrel_add_document (bow_barrel *barrel, 
			     bow_cdoc *cdoc, bow_wv *wv);

/* Call this on a vector-per-document barrel to set the CDOC->PRIOR's
   so that the CDOC->PRIOR's for all documents of the same class sum
   to 1. */
void bow_barrel_set_cdoc_priors_to_class_uniform (bow_barrel *barrel);

/* Given a barrel of documents, create and return another barrel with
   only one vector per class. The classes will be represented as
   "documents" in this new barrel.  CLASSNAMES is an array of strings
   that maps class indices to class names. */
bow_barrel *bow_barrel_new_vpc (bow_barrel *barrel);

/* Like bow_barrel_new_vpc(), but it also sets and normalizes the
   weights appropriately by calling SET_WEIGHTS from the METHOD of
   DOC_BARREL on the `vector-per-class' barrel that will be returned. */
bow_barrel *
bow_barrel_new_vpc_merge_then_weight (bow_barrel *doc_barrel);

/* Same as above, but set the weights in the DOC_BARREL, create the
   `Vector-Per-Class' barrel, and set the weights in the VPC barrel by
   summing weights from the DOC_BARREL. */
bow_barrel *
bow_barrel_new_vpc_weight_then_merge (bow_barrel *doc_barrel);

/* Set the class prior probabilities by counting the number of
   documents of each class. */
void bow_barrel_set_vpc_priors_by_counting (bow_barrel *vpc_barrel,
					    bow_barrel *doc_barrel);

/* Like bow_barrel_new_vpc, but uses both labeled and unlabeled data.
   It uses the class_probs of each doc to determine its class
   membership. The counts in the wi2dvf are set to bogus numbers.  The
   weights of the wi2dvf contain the real information. The normalizer
   of each vpc cdoc is set to the fractional number of documents per
   class.  The word_count of each vpc cdoc is rounded integer for the
   number of documents per class.  The word_count of each document
   cdoc is set to the sum of the counts of its corresponding word
   vector.  This is to get correct numbers for the doc-then-word event
   model.  */
bow_barrel * bow_barrel_new_vpc_using_class_probs (bow_barrel *doc_barrel);

/* Set the class prior probabilities by doing a weighted (by class
   membership) count of the number of labeled and unlabeled documents
   in each class.  This uses class_probs to determine class
   memberships of the documents. */
void bow_barrel_set_vpc_priors_using_class_probs (bow_barrel *vpc_barrel,
						  bow_barrel *doc_barrel);


/* Multiply each weight by the Quinlan `Foilgain' of that word. */
void bow_barrel_scale_weights_by_foilgain (bow_barrel *barrel,
					   bow_barrel *doc_barrel);

/* Multiply each weight by the information gain of that word. */
void bow_barrel_scale_weights_by_infogain (bow_barrel *barrel,
					   bow_barrel *doc_barrel);

/* Modify the BARREL by removing those entries for words that are not
   in the int/str mapping MAP. */
void bow_barrel_prune_words_not_in_map (bow_barrel *barrel,
					bow_int4str *map);

/* Modify the BARREL by removing those entries for words that are in
   the int/str mapping MAP. */
void bow_barrel_prune_words_in_map (bow_barrel *barrel,
				    bow_int4str *map);

/* Modify the BARREL by removing those entries for words that are not
   among the NUM_WORDS_TO_KEEP top words, by information gain.  This
   function is similar to BOW_WORDS_KEEP_TOP_BY_INFOGAIN(), but this
   one doesn't change the word-int mapping. */
void bow_barrel_keep_top_words_by_infogain (int num_words_to_keep, 
					    bow_barrel *barrel,
					    int num_classes);

/* Set the BARREL->WI2DVF->ENTRY[WI].IDF to the sum of the COUNTS for
   the given WI among those documents in the training set. */
void bow_barrel_set_idf_to_count_in_train (bow_barrel *barrel);

/* Return the number of unique words among those documents with TYPE
   tag (train, test, unlabeled, etc) equal to TYPE. */
int bow_barrel_num_unique_words_of_type (bow_barrel *doc_barrel, int type);

/* Write BARREL to the file-pointer FP in a machine independent format. */
void bow_barrel_write (bow_barrel *barrel, FILE *fp);

/* Create and return a `barrel' by reading data from the file-pointer FP. */
bow_barrel *bow_barrel_new_from_data_fp (FILE *fp);

/* Print barrel to FP in human-readable and awk-accessible format. */
void bow_barrel_printf (bow_barrel *barrel, FILE *fp, const char *format);

/* Print as above, but print only those documents for which the
   function PRINT_IF_TRUE returns non-zero. */
void bow_barrel_printf_selected (bow_barrel *barrel, FILE *fp, 
				 const char *format,
				 int (*print_if_true)(bow_cdoc*));

/* Print on stdout the number of times WORD occurs in the various
   docs/classes of BARREL. */
void bow_barrel_print_word_count (bow_barrel *barrel, const char *word);

/* For copying a class barrel.  Doesn't deal with class_probs at all. */
bow_barrel *bow_barrel_copy (bow_barrel *barrel);

/* Return an iterator for the columns of BARREL in class CI */
bow_iterator_double *bow_barrel_iterator_for_ci_new(bow_barrel *barrel,int ci);

/* Free the memory held by BARREL. */
void bow_barrel_free (bow_barrel *barrel);

/* Assign the values of the "word vector entry's" WEIGHT field
   equal to the COUNT. */
void bow_wv_set_weights_to_count (bow_wv *wv, bow_barrel *barrel);

/* Assign weight values appropriate for the different event models.
   For document, weights are 0/1.  For word, weights are same as
   counts.  For doc-then-word, weights are normalized counts. 
   Sets normalizer to be total number of words as appropriate for the 
   event model.*/
void bow_wv_set_weights_by_event_model (bow_wv *wv, bow_barrel *barrel);

/* Assign the values of the "word vector entry's" WEIGHT field
   equal to the COUNT times the word's IDF, taken from the BARREL. */
void bow_wv_set_weights_to_count_times_idf (bow_wv *wv, bow_barrel *barrel);

/* Assign the values of the "word vector entry's" WEIGHT field
   equal to the log(COUNT) times the word's IDF, taken from the BARREL. */
void bow_wv_set_weights_to_log_count_times_idf(bow_wv *wv, bow_barrel *barrel);



/* Parsing headers from email messages. */
/* xxx Eventually all these will be replaced by use of a regular
   expression library. */

/* Read in BUF the characters inside the `<>' of the `Message-Id:'
   field of the email message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int bow_email_get_msgid (FILE *fp, char *buf, int buflen);

/* Read in BUF the characters between the `Received: from ' and the
   following space, and the characters between the ` id ' and the
   following `;' in the file pointer FP.  Return the number of
   characters placed in BUF.  Signal an error if more than BUFLEN
   characters are necessary.  Return -1 if no matching field is
   found. */
int bow_email_get_receivedid (FILE *fp, char *buf, int buflen);

/* Read in BUF the characters inside the `<>' of the `In-Reply-To:'
   field of the email message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more than
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int bow_email_get_replyid (FILE *fp, char *buf, int buflen);

/* Read in BUF the characters inside the `<>' of the `References:'
   field of the news message contain in the file pointer FP.  Return
   the number of characters placed in BUF.  Signal an error if more
   than BUFLEN characters are necessary.  Return -1 if no matching
   field is found. */
int bow_email_get_references (FILE *fp, char *buf, int buflen);

/* Read in BUF the characters inside the `<>' of the
   `Resent-Message-Id:' field of the email message contain in the file
   pointer FP.  Return the number of characters placed in BUF.  Signal
   an error if more than BUFLEN characters are necessary.  Return -1
   if no matching field is found. */
int bow_email_get_resent_msgid (FILE *fp, char *buf, int buflen);

/* Read into BUF the characters inside the `<>' of the `From:' field
   of the email message contain in the file pointer FP.  Return the
   number of characters placed in BUF.  Signal an error if more than
   BUFLEN characters are necessary.  Return -1 if no matching field is
   found. */
int bow_email_get_sender (FILE *fp, char *buf, int buflen);

/* Read into BUF the characters inside the `<>' of the `To:' field
   of the email message contain in the file pointer FP.  Return the
   number of characters placed in BUF.  Signal an error if more than
   BUFLEN characters are necessary.  Return -1 if no matching field is
   found. */
int bow_email_get_recipient (FILE *fp, char *buf, int buflen);

/* Read into BUF the day, month and year of the `Date:' field of the
   email message contain in the file pointer FP.  The format is
   something like `21 Jul 1996'.  Return the number of characters
   placed in BUF.  Signal an error if more than BUFLEN characters are
   necessary.  Return -1 if no matching field is found. */
int bow_email_get_date (FILE *fp, char *buf, int buflen);



/* Progress and error reporting. */

enum bow_verbosity_levels {
  bow_silent = 0,		/* never print anything */
  bow_quiet,			/* only warnings and errors */
  bow_progress,			/* minimal # lines to show progress, use \b */
  bow_verbose,			/* give more status info */
  bow_chatty,			/* stuff most users wouldn't care about */
  bow_screaming			/* every little nit */
};

/* Examined by bow_verbosify() to determine whether to print the message.
   Default is bow_progress. */
extern int bow_verbosity_level;

/* If this is 0, and the message passed to bow_verbosify() contains
   backspaces, then the message will not be printed.  It is useful to
   turn this off when debugging inside an emacs window.  The default
   value is on. */
extern int bow_verbosity_use_backspace;

/* Print the printf-style FORMAT string and ARGS on STDERR, only if
   the BOW_VERBOSITY_LEVEL is equal or greater than the argument 
   VERBOSITY_LEVEL. */
int bow_verbosify (int verbosity_level, const char *format, ...);

/* Print the printf-style FORMAT string and ARGS on STDERR, and abort.
   This function appends a newline to the printed message. */
#define bow_error(FORMAT, ARGS...)			\
({if (bow_verbosity_level > bow_silent)			\
  {							\
    fprintf (stderr, "%s: ", __PRETTY_FUNCTION__);	\
    _bow_error (FORMAT , ## ARGS);			\
  }							\
 else							\
  {							\
    abort ();						\
  }}) 
volatile void _bow_error (const char *format, ...);



/* Memory allocation with error checking. */

/* These "extern inline" functions in this .h file will only be taken
   from here if gcc is optimizing, otherwise, they will be taken from
   identical copies defined in io.c */

void (*bow_malloc_hook) (void *ptr);
void (*bow_realloc_hook) (void *old, void *new);
void (*bow_free_hook) (void *ptr);

#if ! defined (_BOW_MALLOC_INLINE_EXTERN)
#define _BOW_MALLOC_INLINE_EXTERN inline extern
#endif

_BOW_MALLOC_INLINE_EXTERN void *
bow_malloc (size_t s)
{
  void *ret;
#if BOW_MCHECK
  static int mcheck_called = 0;

  if (!mcheck_called)
    {
      int r;
      r = mcheck (NULL);
      assert (r == 0);
      mcheck_called = 1;
    }
#endif /* BOW_MCHECK */
  ret = malloc (s);
  if (!ret)
    bow_error ("Memory exhausted.");
  if (bow_malloc_hook)
    (*bow_malloc_hook) (ret);
  return ret;
}

_BOW_MALLOC_INLINE_EXTERN void * 
bow_realloc (void *ptr, size_t s)
{
  void *ret;
  ret = realloc (ptr, s);
  if (!ret)
    bow_error ("Memory exhausted.");
  if (bow_realloc_hook)
    (*bow_realloc_hook) (ptr, ret);
  return ret;
}

_BOW_MALLOC_INLINE_EXTERN void
bow_free (void *ptr)
{
  if (bow_free_hook)
    bow_free_hook (ptr);
  free (ptr);
}



/* Conveniences for writing and reading. */

/* Version number of file format used to write binary data. */
extern int bow_file_format_version;

/* The default, initial value of above variable.  The above variable will
   take on a different value when reading from binary data archived with 
   a different format version. */
#define BOW_DEFAULT_FILE_FORMAT_VERSION 7

/* Functions for conveniently recording and finding out the format
   version used to write binary data to disk. */
void bow_write_format_version_to_file (const char *filename);
void bow_read_format_version_from_file (const char *filename);

/* Open a file using fopen(), with the same parameters.  Check the
   return value, and raise an error if the open failed.  The caller
   should close the returned file-pointer with fclose(). */
#define bow_fopen(FILENAME, MODE)					\
({									\
  FILE *ret;								\
  ret = fopen (FILENAME, MODE);						\
  if (ret == NULL)							\
    {									\
      if (*MODE == 'r')							\
        {								\
	  perror ("bow_fopen");						\
	  bow_error ("Couldn't open file `%s' for reading", FILENAME);	\
        }								\
      else								\
        {								\
          perror ("bow_fopen");						\
	  bow_error ("Couldn't open file `%s' for writing", FILENAME);	\
        }								\
    }									\
  ret;									\
})

/* These "extern inline" functions in this .h file will only be taken
   from here if gcc is optimizing, otherwise, they will be taken from
   identical copies defined in io.c */

#if ! defined (_BOW_IO_INLINE_EXTERN)
#define _BOW_IO_INLINE_EXTERN inline extern
#endif

/* Write a (int) value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_int (int n, FILE *fp)
{
  int num_written;
  n = htonl (n);
  num_written = fwrite (&n, sizeof (int), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (int);
}

/* Read a (long) value from the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fread_int (int *np, FILE *fp)
{
  int num_read;
  num_read = fread (np, sizeof (int), 1, fp);
  assert (num_read == 1);
  *np = ntohl (*np);
  return num_read * sizeof (int);
}

/* Write a (int) value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_off_t (off_t n, FILE *fp)
{
  int num_written;
  //n = htonl (n);
  num_written = fwrite (&n, sizeof (off_t), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (off_t);
}

/* Read a (long) value from the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fread_off_t (off_t *np, FILE *fp)
{
  int num_read;
  num_read = fread (np, sizeof (off_t), 1, fp);
  assert (num_read == 1);
  //*np = ntohl (*np);
  return num_read * sizeof (off_t);
}


/* Write a (short) value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_short (short n, FILE *fp)
{
  int num_written;
  n = htons (n);
  num_written = fwrite (&n, sizeof (short), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (short);
}

/* Read a (long) value from the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fread_short (short *np, FILE *fp)
{
  int num_read;
  num_read = fread (np, sizeof (short), 1, fp);
  assert (num_read == 1);
  *np = ntohs (*np);
  return num_read * sizeof (short);
}

/* Write a "char*"-string value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_string (const char *s, FILE *fp)
{
  short len;
  int ret;

  if (s)
    len = strlen (s);
  else
    len = 0;
  ret = bow_fwrite_short (len, fp);
  if (len)
    ret += fwrite (s, sizeof (char), len, fp);
  assert (ret == (int)sizeof (short) + len);
  return ret;
}

/* Read a "char*"-string value from the stream FP.  The memory for the
   string will be allocated using bow_malloc(). */
_BOW_IO_INLINE_EXTERN int
bow_fread_string (char **s, FILE *fp)
{
  short len;
  int ret;

  ret = bow_fread_short (&len, fp);
  assert (ret >= 0);
  *s = bow_malloc (len+1);
  if (len)
    ret += fread (*s, sizeof (char), len, fp);
  assert (ret = sizeof (short) + len);
  (*s)[len] = '\0';
  return ret;
}

/* Write a (float) value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_float (float n, FILE *fp)
{
  /* xxx This is not machine-independent! */
  int num_written;
  num_written = fwrite (&n, sizeof (float), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (float);
}

/* Read a (float) value from the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fread_float (float *np, FILE *fp)
{
  /* xxx This is not machine-independent! */
  int num_written;
  num_written = fread (np, sizeof (float), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (float);
}

/* Write a (double) value to the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fwrite_double (double n, FILE *fp)
{
  /* xxx This is not machine-independent! */
  int num_written;
  num_written = fwrite (&n, sizeof (double), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (double);
}

/* Read a (double) value from the stream FP. */
_BOW_IO_INLINE_EXTERN int
bow_fread_double (double *np, FILE *fp)
{
  /* xxx This is not machine-independent! */
  int num_written;
  num_written = fread (np, sizeof (double), 1, fp);
  assert (num_written == 1);
  return num_written * sizeof (double);
}


/* Manipulating a heap of documents */

/* Elements of the heap. */
typedef struct _bow_dv_heap_element {
  bow_dv *dv;                   /* The document vector */
  int wi;                       /* The id of this word */
  int index;                    /* Where we are in the vector currently. */
  int current_di;               /* Might as well keep the key here. */
} bow_dv_heap_element;

/* The heap itself */
typedef struct _bow_dv_heap {
  int length;                   /* How many items in the heap */
  bow_wv *heap_wv;
  int heap_wv_di;
  int last_di;
  bow_dv_heap_element entry[0];	/* The heap */
} bow_dv_heap;


/* Turn an array of bow_dv_heap_elements into a proper heap. The
   heapify function starts working at position i and works down the
   heap.  The heap is indexed from position 1. */
void bow_heapify (bow_dv_heap *wi2dvf, int i);

/* Function to take the top element of the heap - move it's index
   along and place it back in the heap. */
void bow_dv_heap_update (bow_dv_heap *heap);

/* Function to make a heap from all the vectors of documents in the big
   data structure we've built - I hope it all fits.... */
bow_dv_heap *bow_make_dv_heap_from_wi2dvf (bow_wi2dvf *wi2dvf);

/* Function to make a heap from all the vectors of documents in the
   big data structure we've built.  If EVEN_IF_HIDDEN is non-zero,
   then words that have been "hidden" (by feature selection, for
   example) will none-the-less also be included in the WV's returned
   by future calls to the heap; think carefully before you do this! */
bow_dv_heap *bow_make_dv_heap_from_wi2dvf_hidden (bow_wi2dvf *wi2dvf, 
						  int even_if_hidden);

/* Function to create a heap of the vectors of documents associated
   with each word in the word vector. */
bow_dv_heap *bow_make_dv_heap_from_wv (bow_wi2dvf *wi2dvf, bow_wv *wv);


/* Classes for classification.  In some cases each document will
   be in its own class. */

typedef struct _bow_class {
  short class;
  float length;
} bow_class;

/* If non-zero, print to stdout the contribution of each word to
   each class.  Currently implemented only for PrInd. */
extern int bow_print_word_scores;


/* Assigning weights to documents and calculating vector lengths */

/* Normalize the weight-vector for each class (or document) such that
   all vectors have Euclidean length 1. */
void bow_barrel_normalize_weights_by_vector_length (bow_barrel *barrel);

/* Normalize the weight-vector for each class (or document) such that
   in all vectors, the elements of the vector sum to 1. */
void bow_barrel_normalize_weights_by_summing (bow_barrel *barrel);


/* Creating and working with test sets. */

/* Use the state variables from command-line arguments --test-set,
   --train-set, etc, to create the appropriate train/test split */
void bow_set_doc_types (bow_array *docs, int num_classes, 
			bow_int4str *classnames);
void bow_set_doc_types_for_barrel (bow_barrel *barrel);

/* Mark all documents in the array DOCS to be of type TAG. */
void bow_tag_docs (bow_array *docs, int tag);

/* Change documents in the array DOCS of type TAG1 to be of type
   TAG2. Returns the number of tags changed */
int bow_tag_change_tags (bow_array *docs, int tag1, int tag2);


/* This function sets up the data structure so we can step through the word
   vectors for each test document easily. */
bow_dv_heap *bow_test_new_heap (bow_barrel *barrel);

typedef struct _bow_test_wv {
  int di;                          /* The di of this test document. */
  bow_wv wv;                       /* It's associated wv */
} bow_test_wv;

/* Use a heap to efficiently iterate through all the documents that
   satisfy the condition USE_IF_TRUE().  Each call to this function
   provides the next word vector in *WV, returning the `document index' DI of 
   the document contained in *WV.  This function returns -1 when there
   are no more documents left. */
int bow_heap_next_wv (bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv,
		      int (*use_if_true)(bow_cdoc*));

/* Return non-zero iff CDOC has type MODEL. */
int bow_cdoc_is_train (bow_cdoc *cdoc);

/* Return non-zero iff CDOC has type TEST. */
int bow_cdoc_is_test (bow_cdoc *cdoc);

/* Return one */
int bow_cdoc_yes (bow_cdoc *cdoc);
int bow_doc_yes (bow_doc *doc);

/* Return nonzero iff CDOC has type != TEST */
int bow_cdoc_is_nontest (bow_cdoc *cdoc);
int bow_doc_is_nontest (bow_doc *doc);

/* Return nonzero iff CDOC has type == IGNORE */
int bow_cdoc_is_ignore (bow_cdoc *cdoc);

/* Return nonzero iff CDOC has type == VALIDATION */
int bow_cdoc_is_validation (bow_cdoc *cdoc);

/* Return nonzero iff CDOC has type == bow_doc_unlabeled */
int bow_cdoc_is_unlabeled (bow_cdoc *cdoc);

/* Return nonzero iff CDOC has type == bow_doc_pool */
int bow_cdoc_is_pool (bow_cdoc *cdoc);

/* Return nonzero iff CDOC has type == bow_doc_waiting */
int bow_cdoc_is_waiting (bow_cdoc *cdoc);

int bow_cdoc_is_train_or_unlabeled (bow_cdoc *cdoc);

int bow_test_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv);

int bow_nontest_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv);

int bow_model_next_wv(bow_dv_heap *heap, bow_barrel *barrel, bow_wv **wv);



/* Functions for information gain and Foilgain */

/* Return the entropy given counts for each type of element. */
double bow_entropy (float *counts, int num_counts);

/* Return a malloc()'ed array containing an infomation-gain score for
   each word index; it is the caller's responsibility to free this
   array.  NUM_CLASSES should be the total number of classes in the
   BARREL.  The number of entries in the returned array will be found
   in SIZE. */
float *bow_infogain_per_wi_new (bow_barrel *barrel, int num_classes, 
				int *size);

/* Return a word array containing information gain scores, unsorted.
   Only includes words with non-zero infogain. */
bow_wa *bow_infogain_wa (bow_barrel *barrel, int num_classes);

/* Return a word array containing the count for each word, with +/-
   0.1 noise added. */
bow_wa *bow_word_count_wa (bow_barrel *doc_barrel);

/* Return a malloc()'ed array containing an infomation-gain score for
   each word index, but the infogain scores are computing from
   co-occurance of word pairs. */
float *bow_infogain_per_wi_new_using_pairs (bow_barrel *barrel, 
					    int num_classes, int *size);

/* Return a malloc()'ed array containing an Foil-gain score for
   each ``word-index / class pair''.  BARREL must be a `doc_barrel' */
float **bow_foilgain_per_wi_ci_new (bow_barrel *barrel, 
				    int num_classes, int *size);

/* Free the memory allocated in the return value of the function
   bow_foilgain_per_wi_ci_new() */
void bow_foilgain_free (float **fig_per_wi_ci, int num_wi);

/* Print to stdout the sorted results of bow_infogain_per_wi_new().
   It will print the NUM_TO_PRINT words with the highest infogain. */
void bow_infogain_per_wi_print (FILE *fp, bow_barrel *barrel, int num_classes, 
				int num_to_print);

/* Modify the int/word mapping by removing all words except the
   NUM_WORDS_TO_KEEP number of words that have the top information
   gain. */
void bow_words_keep_top_by_infogain (int num_words_to_keep, 
				     bow_barrel *barrel, int num_classes);


/* Parsing news article headers */

/* Function which takes a freshly opened file and reads in the lines up to
   the first blank line, parsing them into header/contents. An sarray is
   returned with the header lines (e.g.Subject) as keys and the entries are
   strings containing the contents. This function _will_ do bad things if not
   used on a news article. */
bow_sarray *bow_parse_news_headers (FILE *fp);

/* Function to take the headers bow_sarray and return a bow_array of strings
   corresponding to the newsgroups. */
bow_array *
bow_headers2newsgroups(bow_sarray *headers);



/* Pseudo-random number generators */

/* This function seeds the random number generator if needed.  Call
   before and random number generator usage, instead of srand */
void bow_random_set_seed();

/* Set the seed to the same value it had the first time it was set
   during this run. */
void bow_random_reset_seed ();

/* Return an double between low and high, inclusive */
double bow_random_double (double low, double high);

/* Return an double between 0 and 1, exclusive */
double bow_random_01 ();



/* Viewable EM functions */

/* Change the weights by sampling from the multinomial distribution
   specified by the training data.  Start from the current values of
   the DV WEIGHTS.  Typically this would be called after iteration 1
   of EM, before the unlabeled documents were included in the
   WEIGHTS. */
void bow_em_perturb_weights (bow_barrel *doc_barrel, bow_barrel *vpc_barrel);



/* argp command-line processing for libbow */

/* Add the options in CHILD to the list of command-line options. */
void bow_argp_add_child (struct argp_child *child);

extern struct argp_child bow_argp_children[];

/* Global variables whose value is set by bow_argp functions, but
   which must be examined by some other function (called later) in
   order to have any effect. */

/* Flag to indicate whether ARG... files should be interpreted as HDB
   databases */
extern int bow_hdb;

/* N for removing all but the top N words by selecting words with
   highest information gain */
extern int bow_prune_vocab_by_infogain_n;

/* N for removing words that occur less than N times */
extern int bow_prune_vocab_by_occur_count_n;

/* The weight-setting and scoring method */
extern bow_method *bow_argp_method;

/* The directory in which we'll store word-vector data. */
extern const char *bow_data_dirname;

/* If non-zero, use equal prior probabilities on classes when setting
   weights, calculating infogain and scoring */
extern int bow_uniform_class_priors;

/* If non-zero, use binary occurrence counts for words. */
extern int bow_binary_word_counts;

/* Don't lex any files with names matching this. */
extern const char *bow_exclude_filename;

/* Pipe the files through this shell command before lexing. */
extern const char *bow_lex_pipe_command;

/* If non-zero, check for eencoding blocks before istext() says that
   the file is text. */
extern int bow_istext_avoid_uuencode;

/* Number of decimal places to print when printing classification scores */
extern int bow_score_print_precision;

/* Which smoothing method to use to avoid zero word probabilities */
extern bow_smoothing bow_smoothing_method;

/* smooth words that occur up to k times in a class for Good-Turing. */
extern int bow_smoothing_goodturing_k;

/* The filename containing the dirichlet alphas */
extern const char *bow_smoothing_dirichlet_filename;

/* The weighting factor for the alphas */
extern float bow_smoothing_dirichlet_weight;


/* Remove words that occur in this many or fewer documents. */
extern int bow_prune_words_by_doc_count_n;

/* Only tokenize words containing `xxx' */
extern int bow_xxx_words_only;

/* Random seed to use for srand, if not equal to -1 */
extern int bow_random_seed;

/* What "event-model" we will use for the probabilistic models. */
typedef enum {
  bow_event_word = 0,		/* the multinomial model */
  bow_event_document,		/* the multi-variate Bernoulli */
  bow_event_document_then_word	/* doc-length-normalized multinomial */
} bow_event_models;

/* What "event-model" we will use for the probabilistic models. 
   Defaults to bow_event_word. */
extern bow_event_models bow_event_model;

/* What "event-model" we will use for calculating information gain of 
   words with classes.  Defaults to bow_event_document. */
bow_event_models bow_infogain_event_model;

/* When using the bow_event_document_then_word event model, we
   normalize the length of all the documents.  This determines the
   normalized length. */
extern int bow_event_document_then_word_document_length;

/* When using --testing-files to set the test/train split, this calls
   the function split.c:bow_test_set_files().  In this function
   compare filenames by their basename only, no their directories. */
extern int bow_test_set_files_use_basename;


/* flex options */

typedef enum
{
  USE_STANDARD_LEXER = 0,
  USE_MAIL_FLEXER,
  USE_TAGGED_FLEXER
} bow_flex_type;

extern bow_flex_type bow_flex_option;

#endif /* __libbow_h_INCLUDE */
