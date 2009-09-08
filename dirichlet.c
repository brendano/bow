#include <stdio.h>
#include <math.h>
#include <malloc.h>
#include <assert.h>
#include <stdlib.h>

double *dirichlet_alphas;
double *dirichlet_counts;
int dirichlet_num_bags;		/* e.g. the number of document classes */
int dirichlet_num_dims;		/* e.g. the vocabulary size */

#define COUNTS(BI,DI) (counts[(BI * num_dims) + DI])

/* Read NUM_BAGS, NUM_DIMS, allocate COUNTS and read them from FP */
void
dirichlet_read_counts (FILE *fp, 
		       double **counts_ptr,
		       int *num_bags_ptr, int *num_dims_ptr)
{
  int i, j, num_bags, num_bags_capacity, num_dims;
  double *counts;

  fscanf (fp, "%d", &num_dims);
  assert (num_dims > 1);
  num_bags_capacity = 32;
  counts = malloc (num_bags_capacity * num_dims * sizeof (double));
  for (i = 0; fscanf (fp, "%lf", counts+i) == 1; i++)
    {
      if (i+1 >= num_bags_capacity * num_dims)
	{
	  num_bags_capacity *= 2;
	  counts = realloc (counts, num_bags_capacity * num_dims
			    * sizeof (double));
	}
      //printf ("count[%d][%d] = %g\n", i/num_dims, i%num_dims, counts[i]);
    }
  if (i % num_dims)
    {
      fprintf (stderr, "Counts must be input in groups of %d\n", num_dims);
      exit (1);
    }
  num_bags = i / num_dims;

  *num_bags_ptr = num_bags;
  *num_dims_ptr = num_dims;
  *counts_ptr = counts;
}

/* Calculate the alpha parameters of a Dirichlet by moment matching
   and place them in ALPHAS.  Return the sum of the alphas. */
double
dirichlet_moment_match (int num_dims, int num_bags, double *counts, 
			double *alphas)
{
  /* The "sample" we will calculate mean and variance for is the
     "proportion" of each token type */
  double *sample_mean = alloca (num_dims * sizeof (double));
  double *sample_variance = alloca (num_dims * sizeof (double));
  double *bag_total = alloca (num_bags * sizeof (double));
  double x, y, alphas_sum;
  int i, j;

  /* Count the total number of words in each bag */
  for (i = 0; i < num_bags; i++)
    {
      bag_total[i] = 0;
      for (j = 0; j < num_dims; j++)
	bag_total[i] += COUNTS(i,j);
      assert (bag_total[i]);
    }

  /* Calculate the sample mean for each dimension, j.  This is
     = (1/#bags) * Sum_bags (count[bag][j]/total[bag])  */
  for (j = 0; j < num_dims; j++)
    {
      sample_mean[j] = 0;
      for (i = 0; i < num_bags; i++)
	sample_mean[j] += COUNTS(i,j) / (bag_total[i] * num_bags);
      assert (sample_mean[j] == sample_mean[j]);
    }
#if 0
  for (j = 0; j < num_dims; j++)
    printf ("sample mean alpha[%d] = %g\n", j, sample_mean[j]);
#endif

  /* Calculate the sample variance for each dimension, j.  This is
     = E[x^2] - E[x]^2
     = [(1/(#bags-1)) * Sum_bags (COUNTS(i,j) / bag_total[i])^2]
       - sample_mean[j]^2 */
  for (j = 0; j < num_dims; j++)
    {
      sample_variance[j] = 0;
      for (i = 0; i < num_bags; i++)
	{
	  x = COUNTS(i,j) / bag_total[i];
	  sample_variance[j] += x * x;
	}
      sample_variance[j] /= num_bags;
      /* We now have E[x^2] */
      sample_variance[j] -= (sample_mean[j] * sample_mean[j]);
      /* We now have E[x^2] - E[x]^2 */
      assert (sample_variance[j] == sample_variance[j]);
    }

  /* Calculate the sum of the alphas */
  x = 0;
  for (j = 0; j < num_dims - 1; j++)
    {
      assert (sample_variance[j] > 0);
      y = ((sample_mean[j] * (1 - sample_mean[j])) / sample_variance[j]) - 1;
      assert (y > 0);
      x += log (y);
      assert (x == x);
    }
  x *= 1.0 / (num_dims - 1.0);
  alphas_sum = exp (x);
  assert (alphas_sum == alphas_sum);

  for (j = 0; j < num_dims; j++)
    alphas[j] = sample_mean[j] * alphas_sum;
  return alphas_sum;
}



double log_gamma(double x)
{
  double result, y, xnum, xden;
  int i;
  static double d1 = -5.772156649015328605195174e-1;
  static double p1[] = { 
    4.945235359296727046734888e0, 2.018112620856775083915565e2, 
    2.290838373831346393026739e3, 1.131967205903380828685045e4, 
    2.855724635671635335736389e4, 3.848496228443793359990269e4, 
    2.637748787624195437963534e4, 7.225813979700288197698961e3 
  };
  static double q1[] = {
    6.748212550303777196073036e1, 1.113332393857199323513008e3, 
    7.738757056935398733233834e3, 2.763987074403340708898585e4, 
    5.499310206226157329794414e4, 6.161122180066002127833352e4, 
    3.635127591501940507276287e4, 8.785536302431013170870835e3
  };
  static double d2 = 4.227843350984671393993777e-1;
  static double p2[] = {
    4.974607845568932035012064e0, 5.424138599891070494101986e2, 
    1.550693864978364947665077e4, 1.847932904445632425417223e5, 
    1.088204769468828767498470e6, 3.338152967987029735917223e6, 
    5.106661678927352456275255e6, 3.074109054850539556250927e6
  };
  static double q2[] = {
    1.830328399370592604055942e2, 7.765049321445005871323047e3, 
    1.331903827966074194402448e5, 1.136705821321969608938755e6, 
    5.267964117437946917577538e6, 1.346701454311101692290052e7, 
    1.782736530353274213975932e7, 9.533095591844353613395747e6
  };
  static double d4 = 1.791759469228055000094023e0;
  static double p4[] = {
    1.474502166059939948905062e4, 2.426813369486704502836312e6, 
    1.214755574045093227939592e8, 2.663432449630976949898078e9, 
    2.940378956634553899906876e10, 1.702665737765398868392998e11, 
    4.926125793377430887588120e11, 5.606251856223951465078242e11
  };
  static double q4[] = {
    2.690530175870899333379843e3, 6.393885654300092398984238e5, 
    4.135599930241388052042842e7, 1.120872109616147941376570e9, 
    1.488613728678813811542398e10, 1.016803586272438228077304e11, 
    3.417476345507377132798597e11, 4.463158187419713286462081e11
  };
  static double c[] = {
    -1.910444077728e-03, 8.4171387781295e-04, 
    -5.952379913043012e-04, 7.93650793500350248e-04, 
    -2.777777777777681622553e-03, 8.333333333333333331554247e-02, 
    5.7083835261e-03
  };
  static double a = 0.6796875;

  if((x <= 0.5) || ((x > a) && (x <= 1.5))) {
    if(x <= 0.5) {
      result = -log(x);
      /*  Test whether X < machine epsilon. */
      if(x+1 == 1) {
	return result;
      }
    }
    else {
      result = 0;
      x = (x - 0.5) - 0.5;
    }
    xnum = 0;
    xden = 1;
    for(i=0;i<8;i++) {
      xnum = xnum * x + p1[i];
      xden = xden * x + q1[i];
    }
    result += x*(d1 + x*(xnum/xden));
  }
  else if((x <= a) || ((x > 1.5) && (x <= 4))) {
    if(x <= a) {
      result = -log(x);
      x = (x - 0.5) - 0.5;
    }
    else {
      result = 0;
      x -= 2;
    }
    xnum = 0;
    xden = 1;
    for(i=0;i<8;i++) {
      xnum = xnum * x + p2[i];
      xden = xden * x + q2[i];
    }
    result += x*(d2 + x*(xnum/xden));
  }
  else if(x <= 12) {
    x -= 4;
    xnum = 0;
    xden = -1;
    for(i=0;i<8;i++) {
      xnum = xnum * x + p4[i];
      xden = xden * x + q4[i];
    }
    result = d4 + x*(xnum/xden);
  }
  /*  X > 12  */
  else {
    y = log(x);
    result = x*(y - 1) - y*0.5 + .9189385332046727417803297;
    x = 1/x;
    y = x*x;
    xnum = c[6];
    for(i=0;i<6;i++) {
      xnum = xnum * y + c[i];
    }
    xnum *= x;
    result += xnum;
  }
  return result;
}


double
dirichlet_multinomial_log_evidence (int num_dims, int num_bags, 
				    double *counts, 
				    double *alphas)
{
  double evidence, alphas_sum;
  double *bag_total = alloca (num_bags * sizeof (double));
  int i, j;

  /* Calculate the sum of the alphas */
  alphas_sum = 0;
  for (j = 0; j < num_dims; j++)
    alphas_sum += alphas[j];

  /* Calculate the bag totals */
  for (i = 0; i < num_bags; i++)
    {
      bag_total[i] = 0;
      for (j = 0; j < num_dims; j++)
	bag_total[i] += COUNTS(i,j);
    }

  evidence = 0;
  for (i = 0; i < num_bags; i++)
    {
      evidence += (log_gamma (alphas_sum)
		   - log_gamma (bag_total[i] + alphas_sum));
      for (j = 0; j < num_dims; j++)
	evidence += log_gamma(COUNTS(i,j) + alphas[j]) - log_gamma(alphas[j]);
    }
  return evidence;
}

void
print_usage (const char *argv[])
{
  fprintf (stderr, "usage: \n");
}

int
main (int argc, const char *argv[])
{
  double sum;
  int i, j, argi, num_classes;
  /* Can be difference from num_dims when there are multiple classes */
  int num_alphas;
  int index_of_correct_class = -1;
  
  num_classes = 0;
  for (argi = 1; argi < argc; argi++)
    {
      if (argv[argi][0] != '-')
	break;
      switch (argv[argi][1])
	{
	case 'c':
	  /* Do classification of bags according to evidence from
	     several different dirichlets */
	  num_classes = atoi (argv[++argi]);
	  break;
	case 'I':
	  index_of_correct_class = atoi (argv[++argi]);
	  break;
	default:
	  fprintf (stderr, "%s: unrecognized option `%s'\n", 
		   argv[0], argv[argi]);
	  print_usage (argv);
	  exit (-1);
	}
    }
  if (argi < argc)
    {
      /* Get the alphas from the command line and then calculate the
	 evidence of the counts read in on stdin */
      int dirichlet_num_dims_capacity = 32;
      double evidence;
      dirichlet_alphas = malloc (dirichlet_num_dims_capacity * sizeof(double));
      for (num_alphas = 0; argi < argc; argi++, num_alphas++)
	{
	  if (num_alphas >= dirichlet_num_dims_capacity)
	    {
	      dirichlet_num_dims_capacity *= 2;
	      dirichlet_alphas = 
		realloc (dirichlet_alphas,
			 dirichlet_num_dims_capacity * sizeof(double));
	    }
	  dirichlet_alphas[num_alphas] = atof (argv[argi]);
	  //printf("alpha[%d] = %g\n",num_alphas,dirichlet_alphas[num_alphas]);
	}
      dirichlet_read_counts (stdin, &dirichlet_counts,
			     &dirichlet_num_bags, &dirichlet_num_dims);
      assert ((num_classes && num_alphas % dirichlet_num_dims == 0)
	      || num_alphas == dirichlet_num_dims);

      if (num_classes)
	{
	  double *ev = alloca (num_classes * sizeof (double));
	  double max_ev;
	  int c, max_c, num_bags_correct;
	  assert (num_alphas == dirichlet_num_dims * num_classes);
	  num_bags_correct = 0;
	  for (i = 0; i < dirichlet_num_bags; i++)
	    {
	      max_c = -1;
	      max_ev = -DBL_MAX;
	      for (c = 0; c < num_classes; c++)
		{
		  ev[c] = dirichlet_multinomial_log_evidence
		    (dirichlet_num_dims, 1, 
		     dirichlet_counts + (i * dirichlet_num_dims),
		     dirichlet_alphas + (c * dirichlet_num_dims));
		  if (ev[c] > max_ev)
		    {
		      max_ev = ev[c];
		      max_c = c;
		    }
		}
	      if (index_of_correct_class != -1 
		  && max_c == index_of_correct_class)
		num_bags_correct++;
	      printf ("bag[%d] class=%d ", i, max_c);
	      for (c = 0; c < num_classes; c++)
		printf ("class[%d]=%g ", c, ev[c]);
	      printf ("\n");
	    }
	  if (index_of_correct_class != -1)
	    printf ("Correct %d/%d = %g\n", 
		    num_bags_correct, dirichlet_num_bags, 
		    ((float)num_bags_correct)/dirichlet_num_bags);
	}
      else
	{
	  evidence = dirichlet_multinomial_log_evidence
	    (dirichlet_num_dims, dirichlet_num_bags,
	     dirichlet_counts, dirichlet_alphas);
	  printf ("log(evidence) = %g\n", evidence);
	}
    }
  else
    {
      /* Read the counts on stdin, then calculate the alphas by 
	 moment matching */
      dirichlet_read_counts (stdin, &dirichlet_counts,
			     &dirichlet_num_bags, &dirichlet_num_dims);
      dirichlet_alphas = malloc (dirichlet_num_dims * sizeof (double));
      sum = dirichlet_moment_match (dirichlet_num_dims, dirichlet_num_bags,
				    dirichlet_counts, dirichlet_alphas);
      fprintf (stderr, "n       = %d\n", dirichlet_num_bags);
      fprintf (stderr, "sum     = %g\np      = ", sum);
      for (j = 0; j < dirichlet_num_dims; j++)
	fprintf (stderr, "%9g ", dirichlet_alphas[j] / sum);
      fprintf (stderr, "\nalphas =\n ");
      for (j = 0; j < dirichlet_num_dims; j++)
	printf ("%15f\n", dirichlet_alphas[j]);
    }

  exit (0);
}
