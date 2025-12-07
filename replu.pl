#!/usr/bin/perl
# replu.pl - Reverse replacement using flags for exact restoration
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

my %backward;
my @backward_keys;

for my $line (@lines) {
    next if $line eq '' || $line !~ /\t/;
    my ($from, $to) = split /\t/, $line, 2;
    next unless defined $from && defined $to;
    
    $from = decode_escapes($from);
    $to = decode_escapes($to);
    
    unless (exists $backward{$to}) {
        $backward{$to} = $from;
        push @backward_keys, $to;
    }
}

die "No replacement pairs found in config\n" unless @backward_keys;

my $bwd_alt = join '|', map { quotemeta } sort { length($b) <=> length($a) } @backward_keys;

# Read input
open my $in, '<:raw', $in_file or die "Cannot open $in_file: $!";
my $data = do { local $/; <$in> };
close $in;
my $in_len = length($data);

# Read flags
open my $flg, '<:raw', $flg_file or die "Cannot open $flg_file: $!";
my $flags = do { local $/; <$flg> };
close $flg;

# Collect all matches using (*FAIL) trick
my @matches;
my %seen_pos;

sub collect_match {
    return if $seen_pos{$-[0]}++;
    push @matches, [$-[0], $1];
}

eval "\$data =~ /(?<=$lb)($bwd_alt)(?=$la)(?{ collect_match() })(*FAIL)/gs;";

my $num_matches = scalar @matches;
my $num_flags = length($flags);
if ($num_matches != $num_flags) {
    warn "Warning: Match count ($num_matches) differs from flag count ($num_flags)\n";
}

# Apply replacements left-to-right with offset tracking
my $offset = 0;

for my $i (0 .. $#matches) {
    my $flag = $i < $num_flags ? substr($flags, $i, 1) : '0';
    next unless $flag eq '1';
    
    my ($orig_pos, $match) = @{$matches[$i]};
    my $adj_pos = $orig_pos + $offset;
    my $repl = $backward{$match};
    
    substr($data, $adj_pos, length($match)) = $repl;
    $offset += length($repl) - length($match);
}

# Write output
open my $out, '>:raw', $out_file or die "Cannot open $out_file: $!";
print $out $data;
close $out;

print STDERR "Input: $in_len bytes, Restored: ", length($data), " bytes, Flags used: $num_matches\n";
