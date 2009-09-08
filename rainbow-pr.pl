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

print STDERR "Reading...\n"; 

while ($line = <STDIN>)
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

print STDERR "Sorting...\n";

@predictions = sort { $b->[0] <=> $a->[0] } @predictions;

if ($bins)
{
    print "The -bins option does not take care of predictions with identical scores properly. Look at the code and see if you want to fix it :).";

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

print STDERR "Coverage  Accuracy\n";


for $prediction (@predictions)
{
    ++$seen_count if $prediction->[2] eq $class;
}

for($index = 0; $index < @predictions; $index++)
{
    $prediction = $predictions[$index];

    ++$predicted_count;
    ++$correct_count if $prediction->[2] eq $class;

    $cov_percent = 100 * $correct_count / $seen_count;

    if (($cov_percent >= $at_interval) &&
        ($index < (@predictions - 1)) &&
        ($prediction->[0] != $predictions[$index + 1]->[0]))
    {
        $acc_percent = 100 * $correct_count / $predicted_count;
        $at_interval += $interval;
        printf("%6.2f  %8.2f (%3d correct of %3d predicted, %7f confidence)\n", $cov_percent, $acc_percent, $correct_count, $predicted_count, $prediction->[0]);
#       print $cov_percent, ' ', $acc_percent, "\n";
        $correct_count = $predicted_count = 0 if $bins;

        # Figure out what our next output point should be
        while ($at_interval <= $cov_percent) {
            $at_interval += $interval;
        }
    }

#    print $seen_count, ' ', $predicted_count, ' ', $correct_count, "\n";
}

$acc_percent = 100 * $correct_count / $predicted_count;
printf("%6.2f  %8.2f (%3d correct of %3d predicted, %7f confidence)\n", $cov_percent, $acc_percent, $correct_count, $predicted_count, $prediction->[0]);
}

sub usage_msg
{
    print STDERR $usage;
    exit 1;
}
