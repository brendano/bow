#!/usr/bin/perl
# The above line is modified by ./Makefile to match the system's
# installed location for Perl.

$usage = <<USAGE;
USAGE: pr [-bins] interval class < andrew-files
USAGE

if ($ARGV[0] eq '-bins')
{
    $bins = 1;
    shift @ARGV;
}
($interval, $class) = @ARGV;

&usage_msg unless $interval && $class;
# Read the first "#0"
$line = <STDIN>;
while ($line) {

# print STDERR "Reading...\n"; 

while (($line = <STDIN>) && ($line !~ /^\#[0-9]+$/))
{

    if (($actual_class, $predicted) = $line =~ /^\S+\s+(\S+)\s+(.*)/)
    {
	for $pred (split(/\s+/, $predicted))
	{
	    if (($predicted_class, $confidence) = $pred =~ /([^:]+):(.*)/)
	    {
		if ($predicted_class eq $class)
		{
		    push(@predictions,
			 [ $confidence, $predicted_class, $actual_class ]);
		    last;
		}
	    }
	}
    }
}

#  print STDERR "Sorting...\n";

@predictions = sort { $b->[0] <=> $a->[0] } @predictions;

if ($bins)
{
    $at_interval = $interval;
    $example_count = @predictions;
    for $prediction (@predictions)
    {
	++$predicted_count;
	++$bin_count;

	++$correct_count if $prediction->[2] eq $class;

	$cov_percent = 100 * $predicted_count / $example_count;

	if ($cov_percent >= $at_interval)
	{
	    $acc_percent = 100 * $correct_count / $bin_count;
	    $at_interval += $interval;
	    printf("%6.2f  %8.2f (%3d correct of %3d predicted, %7f confidence)\n", $cov_percent, $acc_percent, $correct_count, $bin_count, $prediction->[0]);
	    $bin_count = 0;
	    $correct_count = 0;
	}
    }

#    $acc_percent = 100 * $correct_count / $bin_count;
#    printf("%6.2f  %8.2f (%3d correct of %3d predicted, %7f confidence)\n", $cov_percent, $acc_percent, $correct_count, $predicted_count, $prediction->[0]);
}
else
{
$at_interval = $interval;

#print STDERR "Coverage  Accuracy\n";


for $prediction (@predictions)
{
    ++$seen_count if $prediction->[2] eq $class;
}

$printed_be = 0;

for $prediction (@predictions)
{
    ++$predicted_count;
    ++$correct_count if $prediction->[2] eq $class;

    $cov_percent = 100 * $correct_count / $seen_count;
    $acc_percent = 100 * $correct_count / $predicted_count;

    if ($printed_be != 1 && $predicted_count > 1 && $acc_percent < $cov_percent)
    {
	$be_cov = $cov_percent;
	$be_acc = $acc_percent;
	$be_correct = $correct_count;
	$be_predicted = $predicted_count;
	$be_score = $prediction->[0];
	$printed_be = 1;
    }

    if ($cov_percent < $acc_percent)
    {
	$printed_be = 0;
    }

#    print $seen_count, ' ', $predicted_count, ' ', $correct_count, "\n";
}

$acc_percent = 100 * $correct_count / $predicted_count;

printf("%6.2f  %8.2f (%3d correct of %3d predicted, %.30f confidence)\n", $be_cov, $be_acc, $be_correct, $be_predicted, $be_score);

}


} # matches while ($line)

sub usage_msg
{
    print STDERR $usage;
    exit 1;
}
