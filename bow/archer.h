/* archer.h - public declartions for IR frontend to libbow.
   Copyright (C) 1998 Andrew McCallum

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

#ifndef __archer_h_INCLUDE
#define __archer_h_INCLUDE

#include <bow/libbow.h>

/* Position vector Element */
typedef union {
  /* When we write it to disk, it just looks like a `char' */
  unsigned char byte;
  /* The first byte not only must tell us if we must read more bytes
     in order to get all the bits of the offset, but also must tell us
     if this is a "document index" or a "position index". */
  struct _bow_pe {
    unsigned int is_more:1;
    unsigned int is_di:1;
    unsigned int index:6;
  } bits;
  /* The bytes following the first don't need to tell us if they are a
     "document index" or "position index" because the first byte
     already told us. */
  struct _bow_pe_more {
    unsigned int is_more:1;
    unsigned int index:7;
  } bits_more;
} bow_pe;

/* Position Vector in Memory */
typedef struct _bow_pvm {
  int size;
  int read_end;
  int write_end;
  unsigned char contents[0];
} bow_pvm;
 
/* (word, document) Position Vector */
typedef struct _bow_pv {
  //int byte_count;		/* total number of bytes in PV */
  int word_count;		/* total number of word occurrences in PV */
  //int document_count;		/* total number of unique documents in PV */
  off_t seek_start;		/* disk position where this PV starts */
  off_t read_seek_end;		/* disk position from which to read next */
  int read_last_di;		/* doc index last read */
  int read_last_pi;		/* position index last read */
  int read_segment_bytes_remaining;
  int write_last_di;
  int write_last_pi;
  off_t write_seek_last_tailer;
  bow_pvm *pvm;
} bow_pv;

/* (map of) Word Index to Position Vector */
typedef struct _bow_wi2pv {
  const char *pv_filename;
  FILE *fp;
  int num_words;
  int entry_count;
  bow_pv *entry;
} bow_wi2pv;


typedef struct archer_doc {
  bow_doc_type tag;
  const char *filename;
  int word_count;
  int di;
} archer_doc;



bow_wi2pv *bow_wi2pv_new (int capacity, const char *pv_filename);
void bow_wi2pv_free (bow_wi2pv *wi2pv);
void bow_wi2pv_add_wi_di_pi (bow_wi2pv *wi2pv, int wi, int di, int pi);
void bow_wi2pv_rewind (bow_wi2pv *wi2pv);
void bow_wi2pv_wi_next_di_pi (bow_wi2pv *wi2pv, int wi, int *di, int *pi);
void bow_wi2pv_wi_unnext (bow_wi2pv *wi2pv, int wi);
int bow_wi2pv_wi_count (bow_wi2pv *wi2pv, int wi);
void bow_wi2pv_write_to_filename (bow_wi2pv *wi2pv, const char *filename);
bow_wi2pv *bow_wi2pv_new_from_filename (const char *filename);
void bow_wi2pv_print_stats (bow_wi2pv *wi2pv);


/* Fill in PV with the correct initial values, and write the first
   segment header to disk.  What this function does must match what
   bow_pv_add_di_pi() does when it adds a new segment. */
void bow_pv_init (bow_pv *pv, FILE *fp);

/* Add "document index" DI and "position index" PI to PV by writing
   the correct information to FP.  Does not assume that FP is already
   seek'ed to the correct position.  Will add a new PV segment on disk
   if necessary.  Assumes that both DI and PI are greater than or
   equal to the last DI and PI written, respectively. */
void bow_pv_add_di_pi (bow_pv *pv, int di, int pi, FILE *fp);

/* Read the next "document index" DI and "position index" PI.  Does
   not assume that FP is already seek'ed to the correct position.
   Will jump to a new PV segment on disk if necessary. */
void bow_pv_next_di_pi (bow_pv *pv, int *di, int *pi, FILE *fp);

/* Undo the effect of the last call to bow_pv_next_di_pi().  That is,
   make the next call to bow_pv_next_di_pi() return the same DI and PI
   as the last call did.  This function may not be called multiple
   times in a row without calling bow_pv_next_di_pi() in between. */
void bow_pv_unnext (bow_pv *pv);

/* Rewind the read position to the beginning of the PV */
void bow_pv_rewind (bow_pv *pv, FILE *fp);

/* Write the in-memory portion of PV to FP */
void bow_pv_write (bow_pv *pv, FILE *fp, FILE *pvfp);

/* Read the in-memory portion of PV from FP */
void bow_pv_read (bow_pv *pv, FILE *fp);

/* Close and re-open WI2PV's FILE* for its PV's.  This should be done
   after a fork(), since the parent and child will share lseek()
   positions otherwise. */
void bow_wi2pv_reopen_pv (bow_wi2pv *wi2pv);

/* Write the PV to disk */
void bow_pv_flush (bow_pv *pv, FILE *fp);

extern int bow_pvm_total_bytes;

extern int bow_pvm_max_total_bytes;

#endif /* __archer_h_INCLUDE */
