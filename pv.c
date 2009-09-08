/* "Position vector", a (compressed) list of word positions in documents */

/* Copyright (C) 1998 Andrew McCallum

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

#define _FILE_OFFSET_BITS 64

#include <bow/libbow.h>
#include <bow/archer.h>

#define PV_DEBUG 1



/* The total amount of memory consumed by PVM's */
int bow_pvm_total_bytes = 0;

/* The maximum memory we will allow PVM's to take before we flush them
   to disk.  Currently set to 128M */
int bow_pvm_max_total_bytes = 128 * 1024 * 1024;

/* Allocate and return a new PVM that can hold SIZE bytes */
bow_pvm *
bow_pvm_new (int size)
{
  bow_pvm *ret = bow_malloc (sizeof (bow_pvm) + size);
  ret->size = size;
  ret->read_end = 0;
  ret->write_end = 0;
  bow_pvm_total_bytes += sizeof (bow_pvm) + size;
  return ret;
}

/* Increase the capacity of PVM, growing by doubling size until we get
   to 128k, then just grow by 128k increments. */
void
bow_pvm_grow (bow_pvm **pvm)
{
  if ((*pvm)->size < 64 * 1024)
    {
      (*pvm)->size *= 2;
      bow_pvm_total_bytes += (*pvm)->size;
    }
  else
    {
      (*pvm)->size += 64 * 1024;
      bow_pvm_total_bytes += 64 * 1024;
    }
  *pvm = bow_realloc (*pvm, sizeof (bow_pvm) + (*pvm)->size);
}

/* Free the memory associated with the PVM */
void
bow_pvm_free (bow_pvm *pvm)
{
  bow_free (pvm);
}

/* Put the PVM's reader-pointer back to the beginning */
static inline void
bow_pvm_rewind (bow_pvm *pvm)
{
  pvm->read_end = 0;
}



/* PV functions */

/* The first four bytes of a segment are an int that indicate how many
   bytes are allocated in this segment.  The last four bytes of a
   segment are an int that indicates the seek location of the next
   segment.  The read_segment_bytes_remaining does not include
   the size of the two int's. */

/* Always enough for one "document index"/"word index" pair:
   5 bytes == 6+4*7 == 34 bits for di, likewise for pi. */
#define bow_pv_max_sizeof_di_pi (2 * 5)
static int bow_pv_sizeof_first_segment = 2 * bow_pv_max_sizeof_di_pi;

/* Fill in PV with the correct initial values. */
void
bow_pv_init (bow_pv *pv, FILE *fp)
{
  //pv->byte_count = 0;
  pv->word_count = 0;
  //pv->document_count = 0;
  pv->pvm = NULL;
  pv->seek_start = 0; //-1
  pv->read_seek_end = 0;
  pv->read_segment_bytes_remaining = -1;
  pv->read_last_di = -1;
  pv->read_last_pi = -1;
  pv->write_last_di = -1;
  pv->write_last_pi = -1;
  pv->write_seek_last_tailer = 0;	/* This value must match READ_SEEK_END */
}

/* Write this PV's PVM to disk, and free the PVM. */
void
bow_pv_flush (bow_pv *pv, FILE *fp)
{
  off_t seek_new_segment;
  off_t seek_new_tailer;

  if (pv->pvm == NULL || pv->pvm->write_end == 0)
    return;

  /* Seek to the end of the file, which is the position at which this
     segment of the PV will begin. */
  fseeko (fp, 0, SEEK_END);
  seek_new_segment = ftello (fp);
  /* If none of this PV has ever been written to disk, remember this
     position as the start position so that we can rewind there later. */
  if (pv->seek_start == 0) //-1
    {
      pv->seek_start = seek_new_segment;
      pv->read_seek_end = seek_new_segment;
      pv->read_segment_bytes_remaining = pv->pvm->write_end;
    }
  /* Write the "header", which is the number of contents data bytes in
     this segment. */
  bow_fwrite_int (pv->pvm->write_end, fp);
  /* Write the contents data */
  fwrite (pv->pvm->contents, sizeof (unsigned char), pv->pvm->write_end, fp);
  /* Write (a temporary value for) the "tailer".  Later we will put
     here the seek position of the next pv segment on disk. */
  /* xxx Don't actually need a ftello() here.  Do the math instead. */
  seek_new_tailer = ftello (fp);
  bow_fwrite_off_t (0, fp); //-1
  /* If this is not the first time this PV has been flushed, then
     the "tailer" of the previous flushed segment, and write the seek
     position of this segment there. */
  if (pv->write_seek_last_tailer != 0)
    {
      fseeko (fp, pv->write_seek_last_tailer, SEEK_SET);
      bow_fwrite_off_t (seek_new_segment, fp);
    }
  pv->write_seek_last_tailer = seek_new_tailer;
  bow_pvm_total_bytes -= sizeof (bow_pvm) + pv->pvm->size;
  bow_pvm_free (pv->pvm);
  pv->pvm = NULL;
}

/* Write to PVM the unsigned integer I, marked with the special flag
   saying if it is a DI or a PI, (as indicated by IS_DI).  Assumes
   there is enough space there in this PVM to write the info.  Returns
   the number of bytes written. */
static inline int
bow_pvm_write_unsigned_int (bow_pvm *pvm, unsigned int i, int is_di)
{
  bow_pe pe;
  int byte_count = 1;		/* Count already the last byte */

  /* assert (i < (1 < 6+7+7+7+1)); */
  if (is_di)
    pe.bits.is_di = 1;
  else
    pe.bits.is_di = 0;
  if (i > 0x3f)			/* binary = 00111111 */
    {
      pe.bits.is_more = 1;
      pe.bits.index = i & 0x3f;	/* binary = 00111111 */
      pvm->contents[pvm->write_end++] = pe.byte;  /* Write the first byte */
      byte_count++;
      i = i >> 6;
      while (i > 0x7f)		/* binary = 01111111 */
	{
	  pe.bits_more.is_more = 1;
	  pe.bits_more.index = i & 0x7f;
	  pvm->contents[pvm->write_end++] = pe.byte;
	  byte_count++;
	  i = i >> 7;
	}
	pe.bits_more.is_more = 0;
	pe.bits_more.index = i;
	pvm->contents[pvm->write_end++] = pe.byte;
    }
  else
    {
      pe.bits.is_more = 0;
      pe.bits.index = i;
      /* Write the first byte and only */
      pvm->contents[pvm->write_end++] = pe.byte;
    }
  return byte_count;
}

/* Read an unsigned integer into I, and indicate whether it is a
   "document index" or a "position index" by the value of IS_DI.
   Returns the number of bytes read. */
static inline int
bow_pvm_read_unsigned_int (bow_pvm *pvm, unsigned int *i, int *is_di)
{
  bow_pe pe;
  int index;
  int shift = 6;
  int byte_count = 1;

  pe.byte = pvm->contents[pvm->read_end++];
  if (pe.bits.is_di)
    *is_di = 1;
  else
    *is_di = 0;
  index = pe.bits.index;
  while (pe.bits.is_more)
    /* The above test relies on pe.bits.is_more == pe.bits_more.is_more */
    {
      pe.byte = pvm->contents[pvm->read_end++];
      byte_count++;
      index |= pe.bits_more.index << shift;
      shift += 7;
    }
  *i = index;
  return byte_count;
}

/* Read an unsigned integer into I, and indicate whether it is a
   "document index" or a "position index" by the value of IS_DI.
   Assumes that FP is already seek'ed to the correct position. Returns
   the number of bytes read. */
static inline int
bow_pv_read_unsigned_int (unsigned int *i, int *is_di, FILE *fp)
{
  bow_pe pe;
  int index;
  int shift = 6;
  int byte_count = 1;

  pe.byte = fgetc (fp);
  if (pe.bits.is_di)
    *is_di = 1;
  else
    *is_di = 0;
  index = pe.bits.index;
  while (pe.bits.is_more)
    /* The above test relies on pe.bits.is_more == pe.bits_more.is_more */
    {
      pe.byte = fgetc (fp);
      byte_count++;
      index |= pe.bits_more.index << shift;
      shift += 7;
    }
  *i = index;
  return byte_count;
}

#define PV_WRITE_SIZE_INT(N)			\
(((N) < (1 << (6+1)))				\
 ? 1						\
 : (((N) < (1 << (6+7+1)))			\
    ? 2						\
    : (((N) < (1 << (6+7+7+1)))			\
       ? 3					\
       : (((N) < (1 << (6+7+7+7+1)))		\
	  ? 4					\
	  : 5))))

static inline int
bow_pv_write_size_di_pi (bow_pv *pv, int di, int pi)
{
  int size = 0;
  if (pv->write_last_di != di)
    size += PV_WRITE_SIZE_INT (di - pv->write_last_di);
  size += PV_WRITE_SIZE_INT (pi - pv->write_last_pi);
  return size;
}

static inline int
bow_pv_read_size_di_pi (bow_pv *pv, int di, int pi)
{
  int size = 0;
  if (pv->read_last_di != di)
    size += PV_WRITE_SIZE_INT (di - pv->read_last_di);
  size += PV_WRITE_SIZE_INT (pi - pv->read_last_pi);
  return size;
}

/* Write "document index" DI and "position index" PI to FP.  Assumes
   that PV->PVM is already created, and there is space there in this
   PVM segment to write the info.  Returns the number of bytes
   written. */
static inline int
bow_pv_write_next_di_pi (bow_pv *pv, int di, int pi)
{
  int bytes_written = 0;

  if (pv->pvm == NULL)
    pv->pvm = bow_pvm_new (bow_pv_sizeof_first_segment);

  assert (di >= pv->write_last_di);
  if (di != pv->write_last_di)
    {
      bytes_written += 
	bow_pvm_write_unsigned_int (pv->pvm, di - pv->write_last_di, 1);
      pv->write_last_di = di;
      pv->write_last_pi = -1;
    }
  bytes_written +=
    bow_pvm_write_unsigned_int (pv->pvm, pi - pv->write_last_pi, 0);
  pv->write_last_pi = pi;
  return bytes_written;
}

/* Read "document index" DI and "position index" PI from FP.  Assumes
   that FP is already seek'ed to the correct position.  Returns the
   number of bytes read. */
static inline int
bow_pv_read_next_di_pi (bow_pv *pv, int *di, int *pi, FILE *fp)
{
  unsigned int incr;
  int bytes_read = 0;
  int is_di;

  bytes_read += bow_pv_read_unsigned_int (&incr, &is_di, fp);
  if (is_di)
    {
      pv->read_last_di += incr;
      pv->read_last_pi = -1;
      bytes_read += bow_pv_read_unsigned_int (&incr, &is_di, fp);
      assert (!is_di);
    }
  pv->read_last_pi += incr;
  *di = pv->read_last_di;
  *pi = pv->read_last_pi;
  return bytes_read;
}

int
bow_pvm_read_next_di_pi (bow_pv *pv, int *di, int *pi)
{
  unsigned int incr;
  int bytes_read = 0;
  int is_di;

  assert (pv->pvm);

  /* If the special flag was set by bow_pv_unnext(), then return the
     same values returned last time without reading the next entry,
     and unset the flag. */
  if (pv->read_seek_end < 0)
    {
      *di = pv->read_last_di;
      *pi = pv->read_last_pi;
      pv->read_seek_end = -pv->read_seek_end;
      assert (pv->read_seek_end > 0);
      return 0;
    }

  /* If we are about to read from the same location as we would write,
     then we are at the end of the PV.  Return special DI and PI
     values indicate that we are at the end. */
  if (pv->pvm->read_end == pv->pvm->write_end)
    {
      *di = *pi = -1;
      return 0;
    }

  bytes_read += bow_pvm_read_unsigned_int (pv->pvm, &incr, &is_di);
  if (is_di)
    {
      pv->read_last_di += incr;
      pv->read_last_pi = -1;
      bytes_read += bow_pvm_read_unsigned_int (pv->pvm, &incr, &is_di);
      assert (!is_di);
    }
  pv->read_last_pi += incr;
  *di = pv->read_last_di;
  *pi = pv->read_last_pi;
  return bytes_read;
}


/* Add "document index" DI and "position index" PI to PV by writing... */
void
bow_pv_add_di_pi (bow_pv *pv, int di, int pi, FILE *fp)
{
  /* Make sure that PV->PVM definitely has enough room in this PVM
     segment to write another DI and PI.  Will grow the PVM segment if
     necessary.  Assumes that both DI and PI are greater than or equal
     to the last DI and PI written, respectively.  */
  pv->word_count++;
  //if (di != pv->write_last_di) pv->document_count++;
  if (pv->pvm == NULL)
    pv->pvm = bow_pvm_new (bow_pv_sizeof_first_segment);
  if (pv->pvm->size - pv->pvm->write_end < bow_pv_max_sizeof_di_pi)
    bow_pvm_grow (&(pv->pvm));
  //pv->byte_count += 
  bow_pv_write_next_di_pi (pv, di, pi);
}


/* Read the next "document index" DI and "position index" PI.  Does
   not assume that FP is already seek'ed to the correct position.
   Will jump to a new PV segment on disk if necessary. */
void
bow_pv_next_di_pi (bow_pv *pv, int *di, int *pi, FILE *fp)
{
  int byte_count;

  /* If the special flag was set by bow_pv_unnext(), then return the
     same values returned last time without reading the next entry,
     and unset the flag. */
  if (pv->read_seek_end < 0)
    {
      *di = pv->read_last_di;
      *pi = pv->read_last_pi;
      pv->read_seek_end = -pv->read_seek_end;
      assert (pv->read_seek_end > 0);
      return;
    }

  /* If we are about to read from the location of the tailer of the
     last segment written, then we are at the end of the PV on disk.
     Go look for the next entry in memory in the PVM, if the PVM exists. */
  if (pv->read_seek_end == pv->write_seek_last_tailer)
    {
      if (pv->pvm)
	bow_pvm_read_next_di_pi (pv, di, pi);
      else
	*di = *pi = -1;
      return;
    }

  /* Make sure that there was definitely enough room in this segment
     to have written another DI and PI.  If not, then it was written
     in the next segment, so go there and get set up for reading from
     it.  We know that there really is another segment because
     otherwise the above test would have been true. */
  if (pv->read_segment_bytes_remaining == 0)
    {
      off_t seek_new_segment;
      /* Go to the "tailer" of this segment, and read the seek
         position of the next segment. */
      fseeko (fp, pv->read_seek_end, SEEK_SET);
      bow_fread_off_t (&seek_new_segment, fp);
      fseeko (fp, seek_new_segment, SEEK_SET);
      /* Read the number of bytes in this segment, and remember it. */
      bow_fread_int (&(pv->read_segment_bytes_remaining), fp);
      /* Remember the new position from which to read the next DI and PI */
      pv->read_seek_end = ftello (fp);
#if 0
      /* When would this happen now? */
      /* If this segment has not yet been written to, we are at end of PV */
      if (pv->read_seek_end == pv->write_seek_end)
	goto return_end_of_pv;
#endif
    }

  /* Seek to the correct position, read the DI and PI, decrement our
     count of the number of bytes remaining in this segment, and
     update the seek position for reading the next DI and PI. */
  fseeko (fp, pv->read_seek_end, SEEK_SET);
  byte_count =
    bow_pv_read_next_di_pi (pv, di, pi, fp);
  pv->read_segment_bytes_remaining -= byte_count;
  pv->read_seek_end += byte_count;
  assert (pv->read_segment_bytes_remaining >= 0);
}

/* Undo the effect of the last call to bow_pv_next_di_pi().  That is,
   make the next call to bow_pv_next_di_pi() return the same DI and PI
   as the last call did.  This function may not be called multiple
   times in a row without calling bow_pv_next_di_pi() in between. */
void
bow_pv_unnext (bow_pv *pv)
{
  /* Make sure that this function wasn't called two times in a row. */
  assert (pv->read_seek_end > 0);
  pv->read_seek_end = -pv->read_seek_end;
}

/* Rewind the read position to the beginning of the PV */
void
bow_pv_rewind (bow_pv *pv, FILE *fp)
{
  /* If PV is already rewound, just return immediately */
  if (pv->read_seek_end == pv->seek_start + sizeof (int)
      && pv->read_last_di == -1 && pv->read_last_pi == -1)
    return;
  if (pv->seek_start != -1)
    {
      fseeko (fp, pv->seek_start, SEEK_SET);
      bow_fread_int (&(pv->read_segment_bytes_remaining), fp);
      assert (pv->read_segment_bytes_remaining > 0);
      pv->read_seek_end = ftello (fp);
    }
  pv->read_last_di = -1;
  pv->read_last_pi = -1;
  if (pv->pvm)
    bow_pvm_rewind (pv->pvm);
}

/* Write the in-memory portion of PV to FP */
void
bow_pv_write (bow_pv *pv, FILE *fp, FILE *pvfp)
{
  bow_pv_flush (pv, pvfp);
#define FAST_PV_WRITE 1
#if FAST_PV_WRITE
  fwrite (pv, sizeof (bow_pv) - sizeof(void*), 1, fp);
#else
  //bow_fwrite_int (pv->byte_count, fp);
  bow_fwrite_int (pv->word_count, fp);
  //bow_fwrite_int (pv->document_count, fp);
  bow_fwrite_off_t (pv->seek_start, fp);
  bow_fwrite_off_t (pv->read_seek_end, fp);
  bow_fwrite_int (pv->read_last_di, fp);
  bow_fwrite_int (pv->read_last_pi, fp);
  //bow_fwrite_int (pv->read_segment_bytes_remaining, fp);
  bow_fwrite_int (pv->write_last_di, fp);
  bow_fwrite_int (pv->write_last_pi, fp);
  bow_fwrite_off_t (pv->write_seek_last_tailer, fp);
#endif
}

/* Read the in-memory portion of PV from FP */
void
bow_pv_read (bow_pv *pv, FILE *fp)
{
#if FAST_PV_WRITE
  fread (pv, sizeof (bow_pv) - sizeof(void*), 1, fp);
#else
  //bow_fread_int (&pv->byte_count, fp);
  bow_fread_int (&pv->word_count, fp);
  //bow_fread_int (&pv->document_count, fp);
  bow_fread_off_t (&pv->seek_start, fp);
  bow_fread_off_t (&pv->read_seek_end, fp);
  bow_fread_int (&pv->read_last_di, fp);
  bow_fread_int (&pv->read_last_pi, fp);
  //bow_fread_int (&pv->read_segment_bytes_remaining, fp);
  bow_fread_int (&pv->write_last_di, fp);
  bow_fread_int (&pv->write_last_pi, fp);
  bow_fread_off_t (&pv->write_seek_last_tailer, fp);
#endif
}
