#perl

# perl repl.pl book1.cfg book1 book1

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

open( I, "<$ARGV[1]" ) || die;
$a = <I>;
close I;

open( F, ">$ARGV[2].flg" ) || die;
open( R, ">$ARGV[2].out" ) || die;
open( L, ">$ARGV[2].log" ) || die;
#open( B, ">book1.rst" ) || die;


$a0 = $a;
#$flag = "-" x (length($a)*2);
undef @fl;
$p0 = 0; # initial pos
$p = 0; # current pos in output string

sub func {                  
  $p += length($`)-$p0;
#  substr($flag,$p,1)='1';
  $fl[$p]=1;

  $p += length($v);
  $p0 = length($`)+length($&);
}

sub func2 {
  $pp = length($`);

  $cc = $fl[$pp];

  if( not defined $cc ) { $cc=$fl[$pp]=0; };

#  return 1  if $fl[$pp]==1;

#  $fl[$pp] = 0;

#  if( substr($flag,length($`),1) eq '1' ) {
#    return 1;
#  }
#  substr($flag,length($`),1) = '0';
  return 0;
}

print "Forward replacement (book1->book1.out+book1.flg)\n";

eval "\$a=~ s/(?<=$u0)$u(?=$u1)(?{func()})/$v/g;";
$l = length($a);

print "Backward replacement (book1.out+book1.flg->book1.rst)\n";

print R $a;

eval "\$a=~ s/(?<=$u0)$v(?=$u1)(?(?{func2()})|(*FAIL))/$u/g;";

#print B $a;

print "Filtering flags\n";
#$flag = substr($flag,0,$l); # cut to output string length

$flag="";
for( $i=0; $i<$l; $i++ ) {
  $c = $fl[$i];
  $flag .= (defined $c) ? chr(48+$c) : '-';
}

$plog = $flag;
$plog =~ tr/[01]/1/;
$plog =~ tr/\-/0/;
print L $plog;

$flag =~ s/\-//g;
print F $flag;


