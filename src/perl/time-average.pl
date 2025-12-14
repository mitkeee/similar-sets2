#!/usr/bin/perl 
#
# Usage: 
# ./time-average.pl < dataset.result 


# compute the average running time for each query
sub  comp_time_average {
    
   # counting sets
   my $i = 0;
   my $sum = 0;
   
   # thru lines
   while (<STDIN>) {

      # split input line into array @l
      chop;
      my @l = split " ";
      #print "array=".(join ",", @l)."\n";

      # elapsed time is after "="
      if ($l[0] eq "=") {
	  #print $l[1]."\n";
	  $sum += $l[1];
	  $i++;
      }
   }
   
   # print result
   print "average=".int($sum/$i)." ns\n";
}

&comp_time_average;

