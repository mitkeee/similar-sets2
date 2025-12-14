#!/usr/bin/perl 
#
# Usage: 
# ./symb-freq.pl < dataset > dataset.freq

use BerkeleyDB;

BEGIN {
  system "rm *.db\n";
}

# a map from alphabet elements to their frequences
tie %freq, 'BerkeleyDB::Btree',
            -Cachesize => 5000000000,
            -Filename => "freq.db",
            -Flags    => DB_CREATE;  # |DB_RDONLY;

%freq = ();

#
# compute the frequencies of alphabet symbols in a dataset
sub test_symb_freq {
    
   # counting sets
   my $i = 0;

   # thru lines
   while (<STDIN>) {

      # split input line into array @l
      chop;
      my @l = split " ";
      #print "array=".(join ",", @l)."\n";

      for $el (@l) {
         if (!defined $freq{$el}) {
            $freq{$el} = 1
         } else {
            $freq{$el}++;
         } 
      }
   }

   # print frequencies of elphabet elements
   for $el (keys %freq) {
       print $el." ".$freq{$el}."\n";
   }

}

&test_symb_freq;

