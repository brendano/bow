#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <bow/libbow.h>

/* Defaults for command-line arguments. */

/* The number of documents to generate */
int ndocs = 100;

/* The number of words per document */
int nwords_per_doc = 50;

/* Prefix to each filename created */
const char *prefix = NULL;

/* Directory into which to place the documens. */
const char *dirname = NULL;

int noise_vocab_size = 0;
float noise_vocab_fraction = 0;

int print_multinomial_header = 0;
int print_multinomials_only = 0;

/* maximum number of words */
#define MAXN	99999


int df_alphas[MAXN];
int df_alphas_sum;
char *df_words[MAXN];
/* A multinomial sampled from the Dir(alphas) */
float df_p[MAXN];

void
print_usage (const char *argv[])
{
  fprintf (stderr, "usage: %s "
	   "[-d dirname] [-p prefix] [-l doclen] [-n ndocs] [-P]\n"
	   "[-v noisevocabsize] [-f noisevocabfrac] [-m] distfile\n"
	   " Will output NDOCS files each of length DOCLEN with"
	   " filenames having \n"
	   " PREFIX to directory DIRNAME.\n"
	   " With probability NOISEVOCABFRAC, instead of picking"
	   " a word from the\n"
	   " distribution specified by DISTFILE, a word will be chosen"
	   " uniformly \n"
	   " from one of NOISEVOCABSIZE noise-words\n"
	   " If the -P option is given, print each document's multinomial"
	   " as a header.\n"
	   " If the -m option is given, don't create documents, just"
	   " print sampled multinomials\n"
	   , argv[0]);
}

#if 1
/* Return a sample from the Gamma distribution, with parameter IA */
/* From Numerical "Recipes in C", page 292 */
double
bow_gamma_distribution (int ia)
{
  int j;
  double am, e, s, v1, v2, x, y;

  assert (ia >= 1) ;
  if (ia < 6) 
    {
      x = 1.0;
      for (j = 1; j <= ia; j++)
	x *= bow_random_01 ();
      x = - log (x);
    }
  else
    {
      do
	{
	  do
	    {
	      do
		{
		  v1 = 2.0 * bow_random_01 () - 1.0;
		  v2 = 2.0 * bow_random_01 () - 1.0;
		}
	      while (v1 * v1 + v2 * v2 > 1.0);
	      y = v2 / v1;
	      am = ia - 1;
	      s = sqrt (2.0 * am + 1.0);
	      x = s * y + am;
	    }
	  while (x <= 0.0);
	  e = (1.0 + y * y) * exp (am * log (x/am) - s * y);
	}
      while (bow_random_01 () > e);
    }
  return x;
}
#else
//#error This one does not work for small alphas
/* From Larry Wasserman */
double
bow_gamma_distribution (double a)
{
  double b, c, u, v, w, x, y, z;
  int accept;

  b = a-1.0;
  c = 3.0*a - 3.0/4.0;
  accept =0;
  while(accept==0){
    u = bow_random_01 ();
    v = bow_random_01 ();
    if(u==0.0)u=0.00001;
    if(v==0.0)v=0.00001;
    if(u==1.0)u=0.99999;
    if(v==1.0)v=0.99999;
    w = u*(1.0-u);
    y = sqrt(c/w)*(u-0.5);
    x = b+y;
    if(x>=0.0){
      z = 64.0*w*w*w*v*v;
      if(z<=1.0-2.0*y*y/x)accept=1;
      if(accept==0){
	if(log(z)<=2.0*(b*log(x/b)-y))accept=1;
      }
    }
  }
  return x;
}
#endif

int
random_index_from_multinomial (float *p, int size)
{
  float r = bow_random_01 ();
  int k;
  float sum;
  for (k = -1, sum = 0; sum < r; sum += p[k])
    k++;
  return k;
}

void
normalize_multinomial (float *p, int size)
{
  int i;
  float sum = 0;
  for (i = 0; i < size; i++)
    sum += p[i];
  assert (sum);
  for (i = 0; i < size; i++)
    p[i] /= sum;
}

/* Note that, unfortunately, here the ALPHAS must be integers. 
   I don't know how to sample from a Dirichlet with continuous alphas. */
void
random_multinomial_from_dirichlet (int *alphas, int size, float *p)
{
  float p_sum = 0;
  int i;
  for (i = 0; i < size; i++)
    {
      p[i] = bow_gamma_distribution ((double)alphas[i]);
      p_sum += p[i];
    }
  for (i = 0; i < size; i++)
    p[i] /= p_sum;
}

int
main (int argc, const char *argv[])
{
 int argi, N, i=0; 
 float x;
 char s[256];
 FILE *fp;
 char docname[1024];
 const char *distfile;
 int e;

 bow_random_set_seed ();
 for (argi = 1; argi < argc; argi++)
   {
     if (argv[argi][0] != '-')
       break;
     switch (argv[argi][1])
       {
       case 'm':
	 print_multinomials_only = 1;
	 break;
	 break;
       case 'd':
	 dirname = argv[++argi];
	 break;
       case 'p':
	 prefix = argv[++argi];
	 break;
       case 'l':
	 nwords_per_doc = atoi (argv[++argi]);
	 break;
       case 'n':
	 ndocs = atoi (argv[++argi]);
	 break;
       case 'v':
	 noise_vocab_size = atoi (argv[++argi]);
	 break;
       case 'f':
	 noise_vocab_fraction = atof (argv[++argi]);
	 break;
       case 'P':
	 print_multinomial_header = 1;
	 break;
       case '?':
       case 'h':
	 print_usage (argv);
	 exit (0);
       default:
	 fprintf (stderr, "%s: unrecognized option `%s'\n", 
		  argv[0], argv[argi]);
	 print_usage (argv);
	 exit (-1);
       }
   }
 distfile = argv[argi];

 if (dirname && dirname[0] == '/')
   fprintf (stderr, "Output to %s\n", dirname);
 else
   fprintf (stderr, "Output to ./%s\n", dirname);
 /* mkdir (dirname, S_IRWXU | S_IRWXG | S_IRWXO); */

 /* read in prob. distribution */
 fp = fopen (distfile, "r");
 df_alphas_sum = 0;
 for (i = 0; i < MAXN && fscanf(fp, "%f %s", &x, s)==2; i++)
    {
      df_alphas[i] = x;
      assert (df_alphas[i] == x);
      df_alphas_sum += x;
      df_words[i] = strdup (s);
    }
 fclose (fp);
 if (i>=MAXN)
   {
     printf("Error: number of words exceeds %d\n", MAXN);
     exit (-1);
   }
 N = i;
 fprintf(stderr, "Sum of alphas = %d\n", df_alphas_sum);

 if (!print_multinomials_only)
   {
     /* Create the directory if necessary */
     e = mkdir (dirname, 0777);
     if (e != 0 && errno != EEXIST)
       {
	 fprintf (stderr, "Error creating directory `%s'\n", dirname);
	 perror ("dicefactory");
	 exit (-1);
       }
   }

 /* generate documents */
 for (i = 0; i < ndocs; i++)
   {
    /* each with NWORDS_PER_DOC words */
    int j;

    if (!print_multinomials_only)
      {
	if (prefix)
	  sprintf (docname, "%s/%s%05d", dirname, prefix, i);
	else
	  sprintf (docname, "%s/%05d", dirname, i);
	fp = fopen (docname, "w");
	assert (fp);
      }

    /* Sample a multinomial from the Dirichlet */
    random_multinomial_from_dirichlet (df_alphas, N, df_p);

    if (print_multinomials_only)
      {
	int k;
	for (k = 0; k < N; k++)
	  printf ("%f ", df_p[k]);
	printf ("\n");
	continue;
      }

    if (print_multinomial_header)
      {
	int k;
	for (k = 0; k < N; k++)
	  fprintf (fp, "%f %s\n", df_p[k], df_words[k]);
	fprintf (fp, "\n");
      }

    for (j=0; j<nwords_per_doc; j++)
      {
	if (noise_vocab_fraction 
	    && rand()/(float)RAND_MAX > noise_vocab_fraction)
	  {
	    int wn = rand () % noise_vocab_size;
	    fprintf (fp, "noise");
	    /* Convert number WN into alphabetics */
	    while (wn)
	      {
		fprintf (fp, "%c", 'a' + wn % 10);
		wn /= 10;
	      }
	    fprintf (fp, "\n");
	  }
	else
	  {
	    int k = random_index_from_multinomial (df_p, N);
	    fprintf(fp, "%s\n", df_words[k]);
	  }
      }
    fprintf (fp, "\n");
    fclose (fp);
   }
 exit (0);
}
