#!/usr/bin/perl 
#
# Usage: 
# ./max-set-size.pl < dataset


sub test_max_set_size {
    
   my $len; 
   my $max = 0;

   # thru lines
   while (<STDIN>) {

      # split input line into array @l
      chop;
      $len = length;
      if ($len > $max) { 
         $max = $len;
      }
   }

   printf "The maximal size of a line = $max\n";

}

&test_max_set_size;

