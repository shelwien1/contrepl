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

# Apply replacements using conditional with (*FAIL)
my $flag_idx = 0;
my @seen_pos;
my $data_len = length($data);

sub check_flag {
    my $pos = $-[0];
    return 0 if $seen_pos[$pos]++;
    return substr($flags, $flag_idx++, 1) eq '1';
}

eval "\$data =~ s/(?<=$lb)($bwd_alt)(?=$la)(?(?{ check_flag() })|(*FAIL))/\$backward{\$1}/ge;";

# Write output
open my $out, '>:raw', $out_file or die "Cannot open $out_file: $!";
print $out $data;
close $out;

print STDERR "Input: $in_len bytes, Restored: ", length($data), " bytes, Flags used: $flag_idx\n";
