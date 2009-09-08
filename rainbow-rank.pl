#!/usr/bin/perl
# The above line is modified by ./Makefile to match the system's
# installed location for Perl.

# Script to process the output of rainbow program and produce
# statistics about the classification rankings
# Written by Andrew Ng <ayn@ai.mit.edu>

#$nClasses = pop @ARGV;
#die "Specify number of classes on command line" if (!defined $nClasses);

$nClasses = -1;

$foo = <>;
die "Expected first line to be \#triannum" unless ($foo =~ /^\#/);

$nSamples = 0;
while (<>)
  {
  @arr = split(/ /);

  if ($nClasses < 0) {
      $nClasses = @arr - 3;
      for ($i=1; $i <= $nClasses; $i++)
      { $rankCount[$i] = 0; }
  }

  $trueClass = $arr[1];

  $rank = -100;
  for ($i=2; $i <= $#arr; $i++)
	{
	($class, $ignore) = split(/:/, $arr[$i]);
	undef $ignore;
	if ($class eq $trueClass)
		{ $rank = $i-1; }
	}

  die "Bad" if ($rank == -100);

  $rankCount[$rank]++;
  $nSamples++;
  }

print "Rank counts: \n";
for ($i=1; $i <= $nClasses; $i++)
  { print $i, ":", $rankCount[$i], " "; }
print "\n";

$rankCountSum = 0;
$inverseRankSum = 0;
for ($i=1; $i <= $nClasses; $i++)
  {
  $inverseRankSum += (1.0 / $i) * $rankCount[$i];
  $rankCountSum += $rankCount[$i];
  print "With a rank of ", $i, " or better: ", $rankCountSum, "/", $nSamples, " (", $rankCountSum/$nSamples, ")\n";
  }
print "average inverse rank (1.0 means all correct) is: ", $inverseRankSum/$nSamples, "\n";


