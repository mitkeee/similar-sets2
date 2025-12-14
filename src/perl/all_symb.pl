#!/usr/bin/perl 
#
# Usage: 
# ./symb-freq.pl < dataset

# a map from alphabet elements to their frequences
%freq = ();


# compute the frequencies of alphabet symbols in a dataset
sub test_all_symb {
    
   # counting sets
   my $i = 0;

   # thru lines
   while (<STDIN>) {

      # split input line into array @l
      chop;
      my @l = split " ";
      #print "array=".(join ",", @l)."\n";

      for $el (@l) {
         print $el."\n";
      }
   }
}

&test_all_symb;

