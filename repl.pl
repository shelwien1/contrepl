#perl

# perl repl.pl config.txt book1 book1
# Old single-pair format (book1.cfg):
#   line 1: lookbehind pattern
#   line 2: lookahead pattern
#   line 3: search string
#   line 4: replacement string
# New multi-pair format (config.txt):
#   line 1: lookbehind pattern
#   line 2: lookahead pattern
#   lines 3+: contracted<TAB>expanded pairs

undef $/;
open( CFG, "<$ARGV[0]" ) || die;
$cfg = <CFG>;
close CFG;

$cfg =~ s/\r//g;
@c = split /\n/, $cfg;

$u0 = $c[0];
$u1 = $c[1];

# Check if old format (no tab in line 3) or new format (tab-separated pairs)
if ($c[2] !~ /\t/) {
    # Old format: single pair on lines 3-4
    eval "\$u = \"$c[2]\";";
    eval "\$v = \"$c[3]\";";
    %from_map = ($u => $v);
    %to_map = ($v => $u);
    %pattern_idx = ($u => 1);
    @patterns = (quotemeta($u));
    @rev_patterns = (quotemeta($v));
    @contracted = ($u);
    print "Old config format\n";
    print "$u0\n$u1\n<$u>\n<$v>\n\n";
} else {
    # New format: tab-separated pairs starting from line 3
    %from_map = ();
    %to_map = ();
    %pattern_idx = ();
    @pairs = ();
    for ($i = 2; $i <= $#c; $i++) {
        next if $c[$i] eq '';
        ($left, $right) = split(/\t/, $c[$i], 2);
        next if !defined $right;
        # Handle \xNN escape sequences
        $left =~ s/\\x([0-9a-fA-F]{2})/chr(hex($1))/ge;
        $right =~ s/\\x([0-9a-fA-F]{2})/chr(hex($1))/ge;
        $from_map{$left} = $right;
        $to_map{$right} = $left;
        push @pairs, [$left, $right];
    }
    # Sort pairs by contracted length (longest first) to ensure longest match wins
    @pairs = sort { length($b->[0]) <=> length($a->[0]) } @pairs;
    # Assign pattern indices (1-based, 0 means no match)
    for ($i = 0; $i <= $#pairs; $i++) {
        $pattern_idx{$pairs[$i][0]} = $i + 1;
    }
    @patterns = map { quotemeta($_->[0]) } @pairs;
    @rev_patterns = map { quotemeta($_->[1]) } @pairs;
    @contracted = map { $_->[0] } @pairs;
    print "New config format with " . scalar(@pairs) . " pairs\n";
    print "Lookbehind: $u0\n";
    print "Lookahead: $u1\n\n";
}

# Build combined regex pattern
$combined = join('|', @patterns);

open( I, "<$ARGV[1]" ) || die;
$a = <I>;
close I;

open( F, ">$ARGV[2].flg" ) || die;
open( R, ">$ARGV[2].out" ) || die;
open( L, ">$ARGV[2].log" ) || die;

$a0 = $a;
undef @fl;
$p0 = 0; # initial pos in input
$p = 0;  # current pos in output string

sub func {
  my $matched = $1;
  my $repl = $from_map{$matched};
  $repl = $matched if !defined $repl; # fallback
  my $idx = $pattern_idx{$matched};
  $idx = 0 if !defined $idx;

  $p += length($`)-$p0;
  $fl[$p] = $idx;  # Store pattern index instead of just 1

  $p += length($repl);
  $p0 = length($`)+length($&);

  return $repl;
}

print "Forward replacement ($ARGV[1]->$ARGV[2].out+$ARGV[2].flg)\n";

# Use /e modifier to call replacement function
eval "\$a =~ s/(?<=$u0)($combined)(?=$u1)(?{func()})/\$from_map{\$1}/ge;";
$l = length($a);

print "Backward replacement verification\n";

print R $a;

# Regenerate for verification
# Track which positions have been correctly matched
$last_pos2 = -1;
%correct_match = ();
sub func2 {
  my $matched = $1;
  $pp = length($`);

  # Check if this match is for a new position
  my $new_pos = ($pp != $last_pos2);
  $last_pos2 = $pp;

  $cc = $fl[$pp];
  if (not defined $cc) {
    $fl[$pp] = 0;
    return 0;
  }

  if ($cc > 0 && !$correct_match{$pp}) {
    # Verify that the matched expanded form corresponds to the stored pattern index
    my $expected_expanded = $from_map{$contracted[$cc - 1]};
    if ($matched eq $expected_expanded) {
      # Correct pattern matched - mark it
      $correct_match{$pp} = 1;
    }
  }
  return 0;
}

sub finalize_flags {
  # For positions with non-zero flags that weren't correctly matched, set to 0
  for (my $pos = 0; $pos < $l; $pos++) {
    next if !defined $fl[$pos];
    if ($fl[$pos] > 0 && !$correct_match{$pos}) {
      $fl[$pos] = 0;
    }
  }
}

# Build reverse pattern (in corresponding order to forward)
$rev_combined = join('|', @rev_patterns);
eval "\$a =~ s/(?<=$u0)($rev_combined)(?=$u1)(?(?{func2()})|(*FAIL))/\$to_map{\$1}/ge;";

# Finalize flags - set to 0 for positions where correct pattern wasn't matched
finalize_flags();

print "Filtering flags\n";

$flag="";
for( $i=0; $i<$l; $i++ ) {
  $c = $fl[$i];
  $flag .= (defined $c) ? chr(48+$c) : '-';
}

$plog = $flag;
$plog =~ tr/0-9/1/;
$plog =~ tr/\-/0/;
print L $plog;

$flag =~ s/\-//g;
print F $flag;

