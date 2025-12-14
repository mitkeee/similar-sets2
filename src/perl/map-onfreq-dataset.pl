#!/usr/bin/perl
#
# Usage: 
# ./map-onfreq-dataset.pl dataset.freq < dataset > dataset.mapd

use BerkeleyDB;

BEGIN {
  system "rm *.db\n";
}

tie @ds, 'BerkeleyDB::Recno',
         -Cachesize => 5000000000,
         -Filename   => "store.db",
         -Flags      => DB_CREATE;  #|DB_RDONLY;

@ds = ();

# a map from alphabet elements to their frequences
tie %map, 'BerkeleyDB::Btree',
          -Cachesize => 3000000000,
          -Filename => "map.db",
          -Flags    => DB_CREATE;  # |DB_RDONLY;

%map = ();

#BEGIN {
    $freqfn = $ARGV[0];
    #print "freqfn=".$freqfn."\n";
#}

sub map_dataset_onfreq {

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

   # load frequencies and generate a mapping
   open(FR, '<', $freqfn);
   $i = 0;
   while(<FR>) {
      chop;
      my @l = split " ";
      
      $map{$l[0]} = $l[1]; 
   }
   close(FR);

   # map dataset
   for (my $i = 0; $i <= ($ds_size - 1); $i++) {
      my @l = split " ", $ds[$i];

      my @l1 = ();
      my $j = 0;
      for my $el (@l) {
	  $l1[$j] = $map{$el};
	  $j++;
      }

      my $s = join " ", (sort {$a <=> $b} @l1);
      print "$s\n";
   }
}

&map_dataset_onfreq;



