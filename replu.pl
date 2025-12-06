#perl

# perl replu.pl config.txt book1.out book1.flg book1.rst book1.logr
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
    @rev_patterns = (quotemeta($v));
    @contracted = ($u);
    print "Old config format\n";
    print "$u0\n$u1\n<$u>\n<$v>\n\n";
} else {
    # New format: tab-separated pairs starting from line 3
    %from_map = ();
    %to_map = ();
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
    # Sort pairs by contracted length (longest first) - same order as repl.pl
    @pairs = sort { length($b->[0]) <=> length($a->[0]) } @pairs;
    @rev_patterns = map { quotemeta($_->[1]) } @pairs;
    @contracted = map { $_->[0] } @pairs;
    print "New config format with " . scalar(@pairs) . " pairs\n";
    print "Lookbehind: $u0\n";
    print "Lookahead: $u1\n\n";
}

# Build combined reverse regex pattern (matches expanded forms)
$rev_combined = join('|', @rev_patterns);

open( I, "<$ARGV[1]" ) || die; # replaced file (e.g. book1.out)
$a = <I>;
close I;

open( F, "<$ARGV[2]" ) || die; # replace control flags
$flag = <F>;
close F;

open( O, ">$ARGV[3]" ) || die; # restored output

open( L, ">$ARGV[4]" ) || die; # log file

$l = length($a);
undef @pl;
for( $i=0; $i<$l; $i++ ) { $pl[$i]=0; }

# First pass: collect all match positions and their replacements
# DON'T modify $a during this pass to keep positions stable
@matches = ();
$func2_i = 0;
$last_pos = -1;
$last_flag = '0';

sub collect_match {
  my $pos = length($`);
  my $matched = $1;
  my $match_len = length($matched);
  $pl[$pos]=1;

  # Only advance flag index when position changes
  if ($pos != $last_pos) {
    $last_flag = substr($flag,$func2_i++,1);
    $last_pos = $pos;
  }

  # Check if this match should be replaced
  if ($last_flag eq '0') {
    return 0;  # No replacement needed
  }

  # Pattern index is stored in the flag (1-based)
  my $idx = ord($last_flag) - 48;

  if ($idx > 0 && $idx <= scalar(@contracted)) {
    # Get the expected expanded form for this pattern index
    my $expected_expanded = $from_map{$contracted[$idx - 1]};

    # Only replace if the matched text matches the expected expanded form
    if ($matched eq $expected_expanded) {
      my $replacement = $contracted[$idx - 1];
      push @matches, [$pos, $match_len, $replacement];
      # DO NOT return 1 - always return 0 so regex tries all overlapping positions
    }
  }

  return 0;  # Always return 0 to force (*FAIL) and try all positions
}

print "Backward replacement ($ARGV[1]+$ARGV[2]->$ARGV[3])\n";

# Collect matches without replacing
{
  my $a_copy = $a;
  eval "\$a_copy =~ s/(?<=$u0)($rev_combined)(?=$u1)(?(?{collect_match()})|(*FAIL))/\$1/ge;";
}

# Second pass: build result by applying replacements
$result = '';
$pos = 0;
for my $m (@matches) {
  my ($match_pos, $match_len, $replacement) = @$m;
  # Copy text before this match
  $result .= substr($a, $pos, $match_pos - $pos);
  # Add replacement
  $result .= $replacement;
  $pos = $match_pos + $match_len;
}
# Copy remaining text
$result .= substr($a, $pos);

print O $result;

for( $i=0; $i<$l; $i++ ) { print L chr(48+$pl[$i]); }

