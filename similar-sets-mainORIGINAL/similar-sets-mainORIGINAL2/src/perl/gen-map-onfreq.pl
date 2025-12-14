#!/usr/bin/perl
#
# Usage: 
# ./gen-map-onfreq.pl < dataset.freq > dataset.freq.map

use BerkeleyDB;

BEGIN {
  system "rm *.db\n";
}

# a map from alphabet elements to to newly created ids
tie %map, 'BerkeleyDB::Btree',
          -Cachesize => 3000000000,
          -Filename => "map.db",
          -Flags    => DB_CREATE;  # |DB_RDONLY;

%map = ();

sub test_gen_map_onfreq {

   # counting sets
   my $i = 1;
   my (@l,@l1);

   # load frequencies and generate a mapping
   while(<STDIN>) {
      chop;
      my @l = split " ";
      
      #$map{$l[0]} = $i; 
      print $l[0]." ".$i."\n";
      $i++;
   }
}

&test_gen_map_onfreq;



