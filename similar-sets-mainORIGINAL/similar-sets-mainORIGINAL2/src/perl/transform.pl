#!/usr/bin/perl 

# Usage: 
# ./transform < input-file > output-file
#
# Descrption:
# input-file is standard input (stdin).
# output-file is standard output(stdout).
#
# Date: 28/9/2015
# Author: Iztok Savnik

# consts
my $true = 1;
my $false = 0;

# check lines of in file
while (<STDIN>) {

    chop if (/.*\r/);
    chop if (/.*\n/);
    chop if (/.*\r/);

    #my @l = split " ";
    #my $s = join("\,", @l);
 
    print $_."\n";
}

