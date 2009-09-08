/* Calculate Kulback-Leibler Divergence between two word distributions */

#include <bow/libbow.h>

void
print_usage (const char *progname)
{
  fprintf (stderr, "%s [-m] worddistfile1 worddistfile2\n", progname);
  fprintf (stderr, "   -m calculates `KL-Divergence to the mean'\n");
  exit (-1);
}

int
main (int argc, char *argv[])
{
  int doing_larry_loss = 0;
  int doing_kl_div_to_mean = 0;
  FILE *fp;
  float prob;
  static const int word_len = 1024;
  char word[word_len];
  int vocab_size;
  float *dist[2];
  int d;
  double kldiv;
  int wi;
  float dist_sum;

  if (argc == 4)
    {
      if (!strcmp (argv[1], "-m"))
	doing_kl_div_to_mean = 1;
      else if (!strcmp (argv[1], "-l"))
	doing_larry_loss = 1;
      else 
	print_usage (argv[0]);
    }
  else if (argc != 3)
    print_usage (argv[0]);

  /* Read each word distribution to get the vocabulary size */
  for (d = 0; d < 2; d++)
    {
      fp = bow_fopen (argv[argc-2+d], "r");
      while (fscanf (fp, "%f %s", &prob, word) == 2)
	{
	  assert (strlen (word) < word_len);
	  wi = bow_word2int (word);
	}
      fclose (fp);
    }
  /* Insist that no more words be added to the vocabulary. */
  bow_word2int_do_not_add = 1;
  vocab_size = bow_num_words ();
  bow_verbosify (bow_verbose, "Vocabulary size is %d\n", vocab_size);

  /* Initialize the distribution array to zeros. */
  for (d = 0; d < 2; d++)
    {
      dist[d] = (float*) bow_malloc (sizeof(float) * vocab_size);
      for (wi = 0; wi < vocab_size; wi++)
	dist[d][wi] = 0;
    }

  /* Read each word distribution to fill in the distribution array */
  for (d = 0; d < 2; d++)
    {
      fp = bow_fopen (argv[argc-2+d], "r");
      dist_sum = 0;
      while (fscanf (fp, "%f %s", &prob, word) == 2)
	{
	  assert (strlen (word) < word_len);
	  wi = bow_word2int (word);
	  assert (wi != -1);
	  assert (dist[d][wi] == 0);
	  dist[d][wi] = prob;
	  dist_sum += prob;
	}
      if (dist_sum < 0.98 || dist_sum > 1.02)
	bow_error ("Distribution%d sum != 1.0, =%f\n", d, dist_sum);
      fclose (fp);
    }

#if 1
  /* Calculate the value of Larry Wasserman's Loss function.  Assume
     that the first distribution is the correct one. */
  kldiv = 0;
  if (doing_larry_loss)
    {
      double diff;
      for (wi = 0; wi < vocab_size; wi++)
	{
	  diff = dist[1][wi] - dist[0][wi];
	  if (dist[0][wi])
	    kldiv += (diff * diff) / (dist[0][wi] * (1.0 - dist[0][wi]));
	}
      printf ("%g\n", kldiv);
      exit (0);
    }
#endif

  /* Calculate the KL-Div */
  kldiv = 0;
  if (doing_kl_div_to_mean)
    {
      for (wi = 0; wi < vocab_size; wi++)
	kldiv += dist[0][wi] * log (dist[0][wi]
				    / ((dist[0][wi] + dist[1][wi])/2));
    }
  else
    {
      for (wi = 0; wi < vocab_size; wi++)
	if (dist[0][wi])
	  kldiv += dist[0][wi] * log (dist[0][wi]
				      / dist[1][wi]);
    }
  printf ("%g\n", kldiv);

  exit (0);
}
