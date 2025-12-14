#!/usr/bin/perl
#
# Usage: 
# ./sort-dataset.pl < dataset > dataset.sorted 

# data set is stored in 
# %t entries are lists of types

use BerkeleyDB;

BEGIN {
  system "rm *.db\n";
}

tie @ds, 'BerkeleyDB::Recno',
         -Cachesize => 10000000000,
         -Filename   => "store.db",
         -Flags      => DB_CREATE;  #|DB_RDONLY;

@ds = ();

# Sort an array of kv_record-s *arr of length len.
sub quicksort_int {
   my $pa = shift;
   my $low = shift;
   my $high = shift;

   my $len = $high - $low + 1;
   if ($len < 2) { return 0; }
   my $pivot = $$pa[$low + int($len / 2)];

   #   print ">>> pivot=$pivot len=$len low=$low high=$high\n";
   #   for ($k=$low; $k<=$high; $k++) {
   #      print "a[".$k."]=".$$pa[$k]." ";
   #   }
   #   print "\n";

   my ($i,$j,$temp);
   for ($i = $low, $j = $high; ; $i++, $j--) {

      while ($$pa[$i] < $pivot) { $i++; }
      while ($$pa[$j] > $pivot) { $j--; }

      if ($i >= $j) { last; }

      $temp = $$pa[$i];
      $$pa[$i] = $$pa[$j];
      $$pa[$j] = $temp;
   }
   
   if ($len > 2) {

      # if ($i > $j) {
      #    print "<<< pivot=$pivot len=$len low1=$low high1=".($i-1)." low2=".$i." high2=$high\n";
      #    for ($k=$low; $k<=$high; $k++) {
      #        print "a[".$k."]=".$$pa[$k]." ";
      #    }
      #    print "\n";

      &quicksort_int($pa, $low, $i-1);
      &quicksort_int($pa, $i, $high);
   }
}

# Check if seq s1 is greater than seq s2.
sub seq_gt {
   my $s1 = shift;
   my $s2 = shift;

   my $i = 0;
   while (1) {
       if (($i > $#$s1) && ($i > $#$s2)) { return 0; } 
       elsif ($i > $#$s1) { return 0; } 
       elsif ($i > $#$s2) { return 1; } 
       elsif ($$s1[$i] > $$s2[$i]) { return 1; }
       elsif ($$s1[$i] < $$s2[$i]) { return 0; }
       else { $i++; }
   }
}

# Check if seq s1 is smaller (less) than seq s2.
sub seq_lt {
   my $s1 = shift;
   my $s2 = shift;

   my $i = 0;
   while (1) {
       if (($i > $#$s1) && ($i > $#$s2)) { return 0; } 
       elsif ($i > $#$s1) { return 1; } 
       elsif ($i > $#$s2) { return 0; } 
       elsif ($$s1[$i] > $$s2[$i]) { return 0; }
       elsif ($$s1[$i] < $$s2[$i]) { return 1; }
       else { $i++; }
   }
}

# Sort an array of kv_record-s *arr of length len.
sub quicksort_ds {  
   my $low = shift;
   my $high = shift;

   my $len = $high - $low + 1;
   if ($len < 2) { return; }
   my @pivot = split " ", $ds[$low + int($len / 2)];

   #   print ">>> pivot=$pivot len=$len low=$low high=$high\n";
   #   for ($k=$low; $k<=$high; $k++) {
   #      print "a[".$k."]=".$$pa[$k]." ";
   #   }
   #   print "\n";

   my ($i,$j,$temp,@li,@lj);
   for ($i = $low, $j = $high; ; $i++, $j--) {

      @li = split " ", $ds[$i];
      @lj = split " ", $ds[$j];
      while (&seq_lt(\@li, \@pivot)) { $i++; @li = split " ", $ds[$i]; }
      while (&seq_gt(\@lj, \@pivot)) { $j--; @lj = split " ", $ds[$j]; }

      if ($i >= $j) { last; }

      $temp = $ds[$i];
      $ds[$i] = $ds[$j];
      $ds[$j] = $temp;
   }
   
   if ($len > 2) {

      # if ($i > $j) {
      #    print "<<< pivot=$pivot len=$len low1=$low high1=".($i-1)." low2=".$i." high2=$high\n";
      #    for ($k=$low; $k<=$high; $k++) {
      #        print "a[".$k."]=".$$pa[$k]." ";
      #    }
      #    print "\n";

      &quicksort_ds($low, $i-1);
      &quicksort_ds($i, $high);
   }
}

# sort random generated array of int
sub test_sort_int {
   my @a = ();
   my $size = 30000000;
   for my $i (0 .. ($size-1)) {
      $a[$i] = int(rand($size*10));
   }

   #@a = (51,53,52,55,58,41,47,43,42,10,13,19,18,16,31,33,35,36,37,38,3,2,5,6,7,8,1,9,4);
   #print "@a\n";
   &quicksort_int(\@a, 0, @a-1);

   #print "\n\nSORTED\n";
   for $k (0 .. (@a - 1)) {
       print $a[$k]."\n";
   }
}

sub test_sort_dataset {

   # counting sets
   my $i = 0;

   # read and store sets
   my (@l,@l1);
   while (<STDIN>) {

      # split input line into array @l
      chop;
      my @l = split " ";

      # sort a line
      my @l1 = sort {$a <=> $b} @l;
      $ds[$i++] = join " ", @l1; 
   }
   my $ds_size = scalar(@ds);

   # print dataset
   #for (my $i = 0; $i <= ($ds_size - 1); $i++) {
   #   my $s = join " ", $ds[$i];
   #   print "$s\n";
   #}

   &quicksort_ds(0, ($ds_size - 1));

   # print dataset
   for (my $i = 0; $i <= ($ds_size - 1); $i++) {
      my $s = join " ", $ds[$i];
      print "$s\n";
   }
}

# testing dataset: read, sort sets and write
sub test_read_dset {
    
   # counting sets
   my $i = 0;

   # thru lines
   while (<STDIN>) {

      # split input line into array @l
      chop;
      my @l = split " ";
      #print "orign=".$_."\n";
      #print "array=".(join ",", @l)."\n";

      my @l1 = sort {$a <=> $b} @l;
      $ds[$i++] = \@l1; 
   }

   # print dataset
   for (my $i = 0; $i <= $#ds; $i++) {
      my $s = join " ", @{$ds[$i]};
      #print "Size=".@{$ds[$i]}."\n";
      print "$s\n";
      #for (my $j = 0; $j < $#{$ds[$i]}; $j++) {
      #   print $ds[$i][$j]"\n";
      #}
   }
}


&test_sort_dataset;

#my @a = (1,2,4,5,6);
#my @b = (1,2,4,5,7);
#my $c = &seq_lt(\@a, \@b);
#if ($c)
#{ print "true\n"; }
#else
#{ print "false\n"; };


