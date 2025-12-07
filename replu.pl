#perl

# perl replu.pl book1.cfg book1.out book1.flg book1.rst book1.logr

undef $/;
open( CFG, "<$ARGV[0]" ) || die;
$cfg = <CFG>;
close CFG;

$cfg =~ s/\r//g;
@c = split /\n/, $cfg;

$u0 = $c[0];
$u1 = $c[1]; # (?=$u1)
eval "\$u = \"$c[2]\";";
eval "\$v = \"$c[3]\";";

print "$u0\n$u1\n<$u>\n<$v>\n\n";

open( I, "<$ARGV[1]" ) || die; # replaced book1
$a = <I>;
close I;

open( F, "<$ARGV[2]" ) || die; # replace control flags
$flag = <F>;
close F;

open( O, ">$ARGV[3]" ) || die; # restored book1

open( L, ">$ARGV[4]" ) || die;

$l = length($a);
#$plog = "0" x (length($a)*2);
undef @pl;
for( $i=0; $i<$l; $i++ ) { $pl[$i]=0; }

$func2_i = 0;
sub func2 {
#  substr($plog,length($`),1) = '1';
  $pl[length($`)]=1;
  return substr($flag,$func2_i++,1) eq '1';
}


print "Backward replacement (book1.out+book1.flg->book1.rst)\n";

eval "\$a=~ s/(?<=$u0)$v(?=$u1)(?(?{func2()})|(*FAIL))/$u/g;";

print O $a;

#print L substr($plog,0,$l);

for( $i=0; $i<$l; $i++ ) { print L chr(48+$pl[$i]); }


