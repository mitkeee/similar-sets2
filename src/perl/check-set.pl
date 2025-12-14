#!/usr/bin/perl

# Usage: 
# ./check-set sub|sup test-set < input-file
#
# Descrption:
# Prints all sets from input-file that are:
# sub ==> subsets or equal to elemets of test-set, or
# sup ==> supersets or equal to elements of test-set.
# input-file is standard input (stdin).
#
# Date: 26/9/2015
# Author: Iztok Savnik

# consts
my $true = 1;
my $false = 0;

# params
my @p = @ARGV;
my $p1 = $p[0];          # sup|sub
my $p2 = $p[1];          # test-set

# determine sub|sup
my $b = $true;
$b = $true if ($p1 eq "sub");
$b = $false if ($p1 eq "sup");

# test-set as array
my @l1 = split "\,", $p2;

sub subseteq {
    my $pl1 = shift;
    my $pl2 = shift;

    my $e1 = shift @{$pl1};
    my $e2 = shift @{$pl2};

    # $e2 not defined => $pl2 subseteq $pl1
    if (!defined($e2)) { return $true; }
 
    # $e1 not defined => $pl2 not subseteq $pl1
    if (!defined($e1)) { return $false; }

    # $e1 > $e2 => $pl1 does not include $e2
    if ($e1 > $e2) { return $false; }

    # $e1, $e2 equat => take one from both
    if ($e1 eq $e2) { return &subseteq($pl1, $pl2); }

    # $e1 < $e2 => there is chance $e2 is in $pl1
    unshift(@{$pl2}, $e2);
    return &subseteq($pl1, $pl2);
}

# check lines of in file
while (<STDIN>) {

    chop if (/.*\n/);
    chop if (/.*\r/);

    my @l2 = split ",";
    my @l3 = @l1;

    my $s = join("\,", @l2);
    $r = &subseteq(\@l3, \@l2) if ($b);
    $r = &subseteq(\@l2, \@l3) if (!$b);
 
    print $s."\n" if ($r);
}

