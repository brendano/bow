/* Getting and setting bits in a bit-vector. */

/* Copyright (C) 1997, 1998 Andrew McCallum

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

#include <bow/libbow.h>
#include <assert.h>
#include <string.h>		/* for memset() */

#ifndef BITSPERBYTE
#define BITSPERBYTE 8
#endif

/* Allocate, initialize and return a new "bit vector" that is indexed
   by NUM_DIMENSIONS different dimensions.  The size of each dimension
   is given in DIMENSION_SIZES.  The size of the last dimension is
   used as hint for allocating initial memory for the vector, but in
   practice, higher indices for the last dimension can be used later,
   and the bit vector will grow automatically.  Initially, the bit
   vector contains all zeros. */
bow_bitvec *
bow_bitvec_new (int num_dimensions, int *dimension_sizes)
{
  bow_bitvec *ret;
  int i;
  int num_bits = 1;

  ret = bow_malloc (sizeof (bow_bitvec));
  ret->num_dimensions = num_dimensions;
  ret->dimension_sizes = bow_malloc (num_dimensions * sizeof (int));
  for (i = 0; i < num_dimensions; i++)
    {
      ret->dimension_sizes[i] = dimension_sizes[i];
      num_bits *= dimension_sizes[i];
      assert (num_bits > 0);	/* attempt to check for overflow? */
    }
  ret->vector_size = num_bits / BITSPERBYTE + 1;
  ret->vector = bow_malloc (ret->vector_size);
  bow_bitvec_set_all_to_value (ret, 0);
  return ret;
}

/* Set all the bits in the bit vector BV to 0 if value is zero, to 1
   otherwise. */
void
bow_bitvec_set_all_to_value (bow_bitvec *bv, int value)
{
  memset (bv->vector, (value ? ~0 : 0), bv->vector_size);
}

/* If VALUE is non-zero, set the bit at indices INDICES to 1,
   otherwise set it to zero.  Returns the previous value of that
   bit. */
int
bow_bitvec_set (bow_bitvec *bv, int *indices, int value)
{
  int i;
  int bit_index;
  unsigned char *vector_byte;
  int ret;

  for (i = bv->num_dimensions-2, bit_index = indices[bv->num_dimensions-1];
       i >= 0;
       i--)
    {
      bit_index = bit_index *  bv->dimension_sizes[i] + indices[i];
    }
  if (bit_index / BITSPERBYTE >= bv->vector_size)
    {
      /* The bit vector needs to grow to accomodate this entry. */
      int old_size = bv->vector_size;
      int increase = 3;

      if (bv->vector_size * increase / 2 <= bit_index / BITSPERBYTE)
	increase = 4 * bit_index / (BITSPERBYTE * bv->vector_size);
      bv->vector_size *= increase;
      bv->vector_size /= 2;
      bv->dimension_sizes[bv->num_dimensions-1] *= increase;
      bv->dimension_sizes[bv->num_dimensions-1] /= 2;
      bv->vector = bow_realloc (bv->vector, bv->vector_size);
      memset (bv->vector + old_size, 0, bv->vector_size - old_size);
    }
  vector_byte = &(bv->vector[bit_index / BITSPERBYTE]);
  ret = (((*vector_byte) & (0x80 >> (bit_index % BITSPERBYTE))) ? 1 : 0);
  if (value)
    (*vector_byte) |= (0x80 >> (bit_index % BITSPERBYTE));
  else
    (*vector_byte) &= ~(0x80 >> (bit_index % BITSPERBYTE));

  /* Update the bits_set count */
  if ((value == 0) && (ret == 1))
    bv->bits_set--;
  else if ((value != 0) && (ret == 0))
    bv->bits_set++;

  return ret;
}

/* Return the value of the bit at indicies INDICIES. */
int
bow_bitvec_value (bow_bitvec *bv, int *indices)
{
  int i;
  int bit_index;
  unsigned char vector_byte;

  for (i = bv->num_dimensions-2, bit_index = indices[bv->num_dimensions-1];
       i >= 0;
       i--)
    {
      bit_index = bit_index *  bv->dimension_sizes[i] + indices[i];
    }
  /* If this index is beyond the end of the vector, just return 0. */
  if (bit_index / BITSPERBYTE >= bv->vector_size)
    return 0;
  vector_byte = bv->vector[bit_index / BITSPERBYTE];
  return ((vector_byte & (0x80 >> (bit_index % BITSPERBYTE))) ? 1 : 0);
}

/* Free the memory held by the "bit vector" BV. */
void
bow_bitvec_free (bow_bitvec *bv)
{
  bow_free (bv->dimension_sizes);
  bow_free (bv->vector);
  bow_free (bv);
}


#if BOW_BITVEC_TEST
int main (int argc, char *argv[])
{
  bow_bitvec *bv;
  int d1 = 100;
  int d2 = 37;
  int d3 = 12;
  int numbits = d1 * d2 * d3;
  int dims[] = {d1, d2, d3};
  int ind[3];
  int i,j,k;
  int old;

  bv = bow_bitvec_new (3, dims);

  for (i = 0; i < d1; i++)
    {
      ind[0] = i;
      for (j = 0; j < d2; j++)
	{
	  ind[1] = j;
	  for (k = 0; k < d3; k++)
	    {
	      ind[2] = k;
	      old = bow_bitvec_set (bv, ind, (i+j+k)%2);
	      assert (old == 0);
	    }
	}
    }

  for (i = 0; i < d1; i++)
    {
      ind[0] = i;
      for (j = 0; j < d2; j++)
	{
	  ind[1] = j;
	  for (k = 0; k < d3+100; k++)
	    {
	      ind[2] = k;
	      old = bow_bitvec_set (bv, ind, (i+j+k)%3);
	      if (k < d3)
		assert (old == (i+j+k)%2);
	    }
	}
    }

  for (i = 0; i < d1; i++)
    {
      ind[0] = i;
      for (j = 0; j < d2; j++)
	{
	  ind[1] = j;
	  for (k = 0; k < d3+100; k++)
	    {
	      ind[2] = k;
	      old = bow_bitvec_value (bv, ind);
	      assert (old == (((i+j+k)%3) ? 1 : 0));
	    }
	}
    }
  printf ("Test succeeded.\n");
  exit (0);
}
#endif /* BOW_BITVEC_TEST */

/*
Local Variables:
compile-command: "gcc -DBOW_BITVEC_TEST bitvec.c -g -o bvt -L. -lbow"
End:
*/
