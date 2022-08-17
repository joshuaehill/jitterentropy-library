#!/usr/bin/perl

my @indepPvalue = ();
my @IDPvalue = ();

while ( <> ) {
	if(/^Non-Binary Chi-Square Independence: p-value = ([0-9.eE-]+)$/) {
		push(@indepPvalue, $1);
	} elsif(/^Non-Binary Chi-Square Goodness-of-Fit: p-value = ([0-9.eE-]+)$/) {
		push(@IDPvalue, $1);
	}
}

print "Chi-Square Independence p-value median: ";
open(PERCENTILE, "|percentile 0.5") || die "Can't open percentile";
foreach (@indepPvalue) {
	print PERCENTILE "$_\n";
}
close PERCENTILE;

print "Chi-Square Goodness-of-Fit p-value median: ";
open(PERCENTILE, "|percentile 0.5") || die "Can't open percentile";
foreach (@IDPvalue) {
	print PERCENTILE "$_\n";
}
close PERCENTILE;
