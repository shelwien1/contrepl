#!/usr/bin/perl
# repl.pl - Forward replacement with flag generation for reversibility
use strict;
use warnings;
use re 'eval';

die "Usage: $0 config input output flags\n" unless @ARGV == 4;
my ($cfg_file, $in_file, $out_file, $flg_file) = @ARGV;

sub decode_escapes {
    my $s = shift;
    $s =~ s/\\x([0-9A-Fa-f]{2})/chr(hex($1))/ge;
    $s =~ s/\\t/\t/g;
    $s =~ s/\\n/\n/g;
    $s =~ s/\\r/\r/g;
    $s =~ s/\\\\/\\/g;
    return $s;
}

open my $cfg, '<:raw', $cfg_file or die "Cannot open $cfg_file: $!";
my $cfg_data = do { local $/; <$cfg> };
close $cfg;

$cfg_data =~ s/\r\n/\n/g;
my @lines = split /\n/, $cfg_data;

my $lb = shift @lines // '';
my $la = shift @lines // '';

my (%forward, %backward);
my (@forward_keys, @backward_keys);

for my $line (@lines) {
    next if $line eq '' || $line !~ /\t/;
    my ($from, $to) = split /\t/, $line, 2;
    next unless defined $from && defined $to;
    
    $from = decode_escapes($from);
    $to = decode_escapes($to);
    
    $forward{$from} = $to;
    push @forward_keys, $from;
    
    unless (exists $backward{$to}) {
        $backward{$to} = $from;
        push @backward_keys, $to;
    }
}

die "No replacement pairs found in config\n" unless @forward_keys;

my $fwd_alt = join '|', map { quotemeta } sort { length($b) <=> length($a) } @forward_keys;
my $bwd_alt = join '|', map { quotemeta } sort { length($b) <=> length($a) } @backward_keys;

# Read input
open my $in, '<:raw', $in_file or die "Cannot open $in_file: $!";
my $original = do { local $/; <$in> };
close $in;

# Forward replacement with position mapping
my @orig_pos;
my $intermediate = '';
my $last_end = 0;

my $fwd_re = qr/(?<=$lb)($fwd_alt)(?=$la)/s;
while ($original =~ /$fwd_re/g) {
    my ($start, $end, $match) = ($-[0], $+[0], $1);
    if ($start > $last_end) {
        $intermediate .= substr($original, $last_end, $start - $last_end);
        push @orig_pos, ($last_end .. $start - 1);
    }
    my $repl = $forward{$match};
    $intermediate .= $repl;
    push @orig_pos, ($start) x length($repl);
    $last_end = $end;
}
if ($last_end < length($original)) {
    $intermediate .= substr($original, $last_end);
    push @orig_pos, ($last_end .. length($original) - 1);
}

# Collect all backward matches using (*FAIL) trick
my @bwd_matches;
my %seen_pos;

sub collect_match {
    return if $seen_pos{$-[0]}++;
    push @bwd_matches, [$-[0], $1];
}

eval "\$intermediate =~ /(?<=$lb)($bwd_alt)(?=$la)(?{ collect_match() })(*FAIL)/gs;";

# Compute preliminary flags (would this restore original?)
my @preliminary;
for my $m (@bwd_matches) {
    my ($start, $match) = @$m;
    my $repl = $backward{$match};
    my $orig_start = $orig_pos[$start];
    my $orig_substr = substr($original, $orig_start, length($repl));
    push @preliminary, ($orig_substr eq $repl) ? 1 : 0;
}

# Simulate left-to-right replacement, computing position offsets
my @final_flags = (0) x scalar(@bwd_matches);
my $simulated = $intermediate;
my $offset = 0;

for my $i (0 .. $#bwd_matches) {
    next unless $preliminary[$i];
    
    my ($orig_pos, $match) = @{$bwd_matches[$i]};
    my $adj_pos = $orig_pos + $offset;
    my $match_len = length($match);
    
    my $current = substr($simulated, $adj_pos, $match_len);
    if ($current eq $match) {
        my $repl = $backward{$match};
        substr($simulated, $adj_pos, $match_len) = $repl;
        $offset += length($repl) - $match_len;
        $final_flags[$i] = 1;
    }
}

my $flags = join '', @final_flags;

# Write outputs
open my $out, '>:raw', $out_file or die "Cannot open $out_file: $!";
print $out $intermediate;
close $out;

open my $flg, '>:raw', $flg_file or die "Cannot open $flg_file: $!";
print $flg $flags;
close $flg;

print STDERR "Original: ", length($original), " bytes, Output: ", length($intermediate), " bytes, Flags: ", length($flags), "\n";
