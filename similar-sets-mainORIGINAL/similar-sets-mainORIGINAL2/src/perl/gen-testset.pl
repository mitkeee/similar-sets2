#!/usr/bin/perl
#
# Usage: 
# ./gen-testset.pl procent < dataset > testset

use BerkeleyDB;

BEGIN {
  system "rm *.db\n";
}

# store dataset
tie @ds, 'BerkeleyDB::Recno',
         -Cachesize => 5000000000,
         -Filename   => "store.db",
         -Flags      => DB_CREATE;  #|DB_RDONLY;
@ds = ();

# set of indexes generated so far
%ixs = ();

# percent of dataset generated
$percent = $ARGV[0]/100;

# main test proc
sub gen_testset {

   # counting sets
   my $i = 0;

   # read and store sets from dataset
   my @l = ();
   my $l = "";
   %ixs = ();
   
   while (<STDIN>) {

      # split input line into array @l
      #chop;
      chop($_);
      #my @l = split " ";

      # sort a line
      #my @l1 = sort {$a <=> $b} @l;
      #$ds[$i++] = join " ", @l; 
      $ds[$i++] = $_;
      #print "$_\n";
   }
   my $ds_size = scalar(@ds);

   # generate testset
   my $iter_num = int($ds_size * $percent + 1);
   my $rand_num = 0;
   
   while ($iter_num > 0) {

      $rand_num = int(rand($ds_size));
      if (!defined $ixs{$rand_num}) {

	 print $ds[$rand_num]."\n";;
         $iter_num--;
	 $ixs{$rand_num} = 1;
      }


   }
}

&gen_testset;



