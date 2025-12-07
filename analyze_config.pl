#!/usr/bin/perl
# analyze_config.pl - Analyze config file for contradictions and output fixed version
# Usage: perl analyze_config.pl <input_config> <output_config>

use strict;
use warnings;
use Encode qw(decode encode);

if (@ARGV < 2) {
    die "Usage: $0 <input_config> <output_config>\n";
}

my ($input_file, $output_file) = @ARGV;

# Read input config
open(my $fh, '<:raw', $input_file) or die "Cannot open $input_file: $!\n";
my @lines = <$fh>;
close($fh);

# Remove trailing newlines and carriage returns (handle Windows CRLF)
s/[\r\n]+$// for @lines;

if (@lines < 2) {
    die "Config file must have at least 2 lines (lookbehind and lookahead patterns)\n";
}

my $lookbehind_orig = $lines[0];
my $lookahead_orig = $lines[1];

# Validate and fix assertion patterns
# Returns (working_pattern, output_pattern)
# working_pattern is used for matching, output_pattern is written to output file
sub validate_pattern {
    my ($pattern, $name) = @_;

    # Empty pattern - use empty non-capturing group for matching
    if (!defined $pattern || $pattern eq '') {
        print "$name: empty, using (?:) for matching\n";
        return ('(?:)', $pattern // '');
    }

    # Try to compile as a regex
    eval {
        my $test = "test";
        $test =~ /(?<=$pattern)/;
    };
    if ($@) {
        print "$name: '$pattern' is not a valid regex: $@";
        print "  -> Replacing with (?:) for matching\n";
        return ('(?:)', '(?:)');  # Also fix in output since original is broken
    }

    return ($pattern, $pattern);
}

my ($lookbehind, $lookbehind_out) = validate_pattern($lookbehind_orig, "Lookbehind");
my ($lookahead, $lookahead_out) = validate_pattern($lookahead_orig, "Lookahead");

print "Lookbehind pattern (for matching): $lookbehind\n";
print "Lookahead pattern (for matching): $lookahead\n";

# Parse pairs (starting from line 3, index 2)
my @pairs;
my %left_to_idx;   # left string -> array of pair indices
my %right_to_idx;  # right string -> array of pair indices

for my $i (2 .. $#lines) {
    my $line = $lines[$i];
    next if $line eq '';  # Skip empty lines

    # Split by tab
    my @parts = split(/\t/, $line, 2);
    if (@parts != 2) {
        print "Warning: Line ${\($i+1)} doesn't have exactly 2 tab-separated fields, skipping\n";
        next;
    }

    my ($left, $right) = @parts;

    # Decode escape sequences like \xE0
    $left = decode_escapes($left);
    $right = decode_escapes($right);

    my $idx = scalar(@pairs);
    push @pairs, { left => $left, right => $right, line => $i + 1, removed => 0 };

    push @{$left_to_idx{$left}}, $idx;
    push @{$right_to_idx{$right}}, $idx;
}

print "Parsed " . scalar(@pairs) . " pairs\n\n";

# Check for contradictions

my @issues;

# 1. Check for duplicate left-side strings (same left maps to different right)
print "=== Checking for duplicate left-side strings ===\n";
for my $left (keys %left_to_idx) {
    my @indices = @{$left_to_idx{$left}};
    if (@indices > 1) {
        my @rights = map { $pairs[$_]{right} } @indices;
        my %unique_rights = map { $_ => 1 } @rights;
        if (keys %unique_rights > 1) {
            print "CONFLICT: Left string '$left' maps to multiple different right strings:\n";
            for my $idx (@indices) {
                print "  Line $pairs[$idx]{line}: '$left' -> '$pairs[$idx]{right}'\n";
            }
            push @issues, { type => 'dup_left', indices => \@indices, key => $left };
        } else {
            print "DUPLICATE (same mapping): Left string '$left' appears multiple times\n";
            # Remove all but first
            for my $idx (@indices[1..$#indices]) {
                $pairs[$idx]{removed} = 1;
                $pairs[$idx]{reason} = "Duplicate of line $pairs[$indices[0]]{line}";
            }
        }
    }
}

# 2. Check for duplicate right-side strings (same right maps to different left)
print "\n=== Checking for duplicate right-side strings ===\n";
for my $right (keys %right_to_idx) {
    my @indices = @{$right_to_idx{$right}};
    if (@indices > 1) {
        my @lefts = map { $pairs[$_]{left} } @indices;
        my %unique_lefts = map { $_ => 1 } @lefts;
        if (keys %unique_lefts > 1) {
            print "CONFLICT: Right string '$right' maps to multiple different left strings:\n";
            for my $idx (@indices) {
                print "  Line $pairs[$idx]{line}: '$pairs[$idx]{left}' -> '$right'\n";
            }
            push @issues, { type => 'dup_right', indices => \@indices, key => $right };
        }
    }
}

# 3. Check for substring conflicts
# A conflict occurs if applying forward then reverse (or reverse then forward)
# doesn't return the original string
print "\n=== Checking for substring/overlap conflicts ===\n";

# Build replacement maps from non-removed pairs
sub build_maps {
    my @active_pairs = grep { !$_->{removed} } @pairs;

    my %forward;  # left -> right
    my %reverse;  # right -> left

    for my $p (@active_pairs) {
        $forward{$p->{left}} = $p->{right};
        $reverse{$p->{right}} = $p->{left};
    }

    return (\%forward, \%reverse);
}

# Apply replacements to a string (with assertions)
# Sort by length descending to match longer strings first
sub apply_replacement {
    my ($str, $map_ref, $lookbehind, $lookahead) = @_;
    my %map = %$map_ref;

    return $str unless %map;

    # Build regex: sort keys by length (longest first) to avoid partial matches
    my @keys = sort { length($b) <=> length($a) } keys %map;

    # Escape special regex characters in keys
    my @escaped_keys = map { quotemeta($_) } @keys;

    # Build the pattern with lookbehind and lookahead
    my $pattern = '(?<=' . $lookbehind . ')(' . join('|', @escaped_keys) . ')(?=' . $lookahead . ')';

    # Apply replacement
    $str =~ s/$pattern/$map{$1}/ge;

    return $str;
}

# Apply replacements WITHOUT assertions (for conflict testing)
sub apply_replacement_no_assert {
    my ($str, $map_ref) = @_;
    my %map = %$map_ref;

    return $str unless %map;

    # Build regex: sort keys by length (longest first) to avoid partial matches
    my @keys = sort { length($b) <=> length($a) } keys %map;

    # Escape special regex characters in keys
    my @escaped_keys = map { quotemeta($_) } @keys;

    # Build the pattern without assertions
    my $pattern = '(' . join('|', @escaped_keys) . ')';

    # Apply replacement
    $str =~ s/$pattern/$map{$1}/ge;

    return $str;
}

# Test all strings for round-trip consistency
# Apply both regexps (without assertions) to all strings from both sides
sub find_conflicts {
    my ($forward_ref, $reverse_ref) = @_;
    my @conflicts;

    # Test LEFT side strings: left -> forward -> reverse -> should equal left
    for my $left (keys %$forward_ref) {
        my $right = $forward_ref->{$left};

        # Apply forward to get the right side
        my $after_forward = apply_replacement_no_assert($left, $forward_ref);
        # Apply reverse to get back to left
        my $after_reverse = apply_replacement_no_assert($after_forward, $reverse_ref);

        if ($after_reverse ne $left) {
            push @conflicts, {
                type => 'forward_then_reverse',
                side => 'left',
                original => $left,
                expected_right => $right,
                after_forward => $after_forward,
                after_reverse => $after_reverse,
            };
        }
    }

    # Test RIGHT side strings: right -> reverse -> forward -> should equal right
    for my $right (keys %$reverse_ref) {
        my $left = $reverse_ref->{$right};

        # Apply reverse to get the left side
        my $after_reverse = apply_replacement_no_assert($right, $reverse_ref);
        # Apply forward to get back to right
        my $after_forward = apply_replacement_no_assert($after_reverse, $forward_ref);

        if ($after_forward ne $right) {
            push @conflicts, {
                type => 'reverse_then_forward',
                side => 'right',
                original => $right,
                expected_left => $left,
                after_reverse => $after_reverse,
                after_forward => $after_forward,
            };
        }
    }

    # Test for prefix overlap conflicts
    # Example: "couldn't" -> "could not" and "couldn't've" -> "could not have"
    # The string "couldn't have" (not in config) becomes:
    #   forward: "could not have" (because "couldn't" -> "could not")
    #   reverse: "couldn't've" (because "could not have" -> "couldn't've")
    # But "couldn't've" != "couldn't have" -> CONFLICT

    for my $left1 (keys %$forward_ref) {
        my $right1 = $forward_ref->{$left1};

        for my $right2 (keys %$reverse_ref) {
            next if $right1 eq $right2;
            my $left2 = $reverse_ref->{$right2};

            # Check if right1 is a proper prefix of right2
            if (length($right2) > length($right1) &&
                substr($right2, 0, length($right1)) eq $right1) {

                my $suffix = substr($right2, length($right1));
                my $test_string = $left1 . $suffix;  # e.g., "couldn't" + " have" = "couldn't have"

                # Apply forward
                my $after_forward = apply_replacement_no_assert($test_string, $forward_ref);
                # Apply reverse
                my $after_reverse = apply_replacement_no_assert($after_forward, $reverse_ref);

                if ($after_reverse ne $test_string) {
                    push @conflicts, {
                        type => 'prefix_overlap',
                        original => $test_string,
                        shorter_left => $left1,
                        shorter_right => $right1,
                        longer_right => $right2,
                        longer_left => $left2,
                        after_forward => $after_forward,
                        after_reverse => $after_reverse,
                    };
                }
            }

            # Also check suffix overlap: right1 is a proper suffix of right2
            if (length($right2) > length($right1) &&
                substr($right2, -length($right1)) eq $right1) {

                my $prefix = substr($right2, 0, length($right2) - length($right1));
                my $test_string = $prefix . $left1;  # e.g., "something " + "couldn't"

                # Apply forward
                my $after_forward = apply_replacement_no_assert($test_string, $forward_ref);
                # Apply reverse
                my $after_reverse = apply_replacement_no_assert($after_forward, $reverse_ref);

                if ($after_reverse ne $test_string) {
                    push @conflicts, {
                        type => 'suffix_overlap',
                        original => $test_string,
                        shorter_left => $left1,
                        shorter_right => $right1,
                        longer_right => $right2,
                        longer_left => $left2,
                        after_forward => $after_forward,
                        after_reverse => $after_reverse,
                    };
                }
            }
        }
    }

    # Remove duplicate conflicts
    my %seen;
    @conflicts = grep {
        my $key = "$_->{type}:$_->{original}";
        !$seen{$key}++;
    } @conflicts;

    return @conflicts;
}

# Resolve conflicts by testing which pairs cause problems
sub resolve_issues {
    # First handle the direct duplicates
    for my $issue (@issues) {
        if ($issue->{type} eq 'dup_left' || $issue->{type} eq 'dup_right') {
            # Keep the first one, remove the rest
            my @indices = @{$issue->{indices}};
            for my $idx (@indices[1..$#indices]) {
                next if $pairs[$idx]{removed};
                $pairs[$idx]{removed} = 1;
                $pairs[$idx]{reason} = "Conflicting $issue->{type} for '$issue->{key}'";
                print "Removing line $pairs[$idx]{line}: '$pairs[$idx]{left}' <-> '$pairs[$idx]{right}' (conflict)\n";
            }
        }
    }
}

resolve_issues();

# Now check for substring conflicts iteratively
my $iteration = 0;
my $max_iterations = 100;  # Prevent infinite loops

while ($iteration < $max_iterations) {
    $iteration++;
    my ($forward_ref, $reverse_ref) = build_maps();
    my @conflicts = find_conflicts($forward_ref, $reverse_ref);

    last unless @conflicts;

    print "\nIteration $iteration: Found " . scalar(@conflicts) . " conflicts\n";

    # For each conflict, identify which pair(s) are involved
    my %problematic_pairs;

    for my $conflict (@conflicts) {
        if ($conflict->{type} eq 'forward_then_reverse') {
            print "  $conflict->{type} ($conflict->{side}): '$conflict->{original}' -> '$conflict->{after_forward}' -> '$conflict->{after_reverse}'\n";
        } elsif ($conflict->{type} eq 'reverse_then_forward') {
            print "  $conflict->{type} ($conflict->{side}): '$conflict->{original}' -> '$conflict->{after_reverse}' -> '$conflict->{after_forward}'\n";
        } elsif ($conflict->{type} eq 'prefix_overlap' || $conflict->{type} eq 'suffix_overlap') {
            print "  $conflict->{type}: '$conflict->{original}' -> '$conflict->{after_forward}' -> '$conflict->{after_reverse}'\n";
            print "    Shorter pair: '$conflict->{shorter_left}' <-> '$conflict->{shorter_right}'\n";
            print "    Longer pair:  '$conflict->{longer_left}' <-> '$conflict->{longer_right}'\n";
        }

        # Find which pairs are involved in causing this conflict
        for my $i (0 .. $#pairs) {
            next if $pairs[$i]{removed};
            my $left = $pairs[$i]{left};
            my $right = $pairs[$i]{right};

            if ($conflict->{type} eq 'forward_then_reverse') {
                if ($conflict->{after_forward} =~ /\Q$right\E/ &&
                    $conflict->{after_forward} ne $conflict->{original} &&
                    $conflict->{original} ne $left) {
                    $problematic_pairs{$i}++;
                }
            } elsif ($conflict->{type} eq 'reverse_then_forward') {
                if ($conflict->{after_reverse} =~ /\Q$left\E/ &&
                    $conflict->{after_reverse} ne $conflict->{original} &&
                    $conflict->{original} ne $right) {
                    $problematic_pairs{$i}++;
                }
            } elsif ($conflict->{type} eq 'prefix_overlap' || $conflict->{type} eq 'suffix_overlap') {
                # Remove the LONGER pair to keep the more common shorter contractions
                # e.g., keep "shouldn't" <-> "should not", remove "shouldn't've" <-> "should not have"
                if ($left eq $conflict->{longer_left} && $right eq $conflict->{longer_right}) {
                    $problematic_pairs{$i}++;
                }
            }
        }
    }

    # If we found problematic pairs, remove them
    if (%problematic_pairs) {
        # Remove the pair that causes the most conflicts
        my @sorted = sort { $problematic_pairs{$b} <=> $problematic_pairs{$a} } keys %problematic_pairs;
        my $to_remove = $sorted[0];
        $pairs[$to_remove]{removed} = 1;
        $pairs[$to_remove]{reason} = "Causes substring conflict";
        print "  -> Removing line $pairs[$to_remove]{line}: '$pairs[$to_remove]{left}' <-> '$pairs[$to_remove]{right}'\n";
    } else {
        # Couldn't identify specific pair, remove one from the first conflict
        # Find the pair that matches the original string in the conflict
        my $conflict = $conflicts[0];
        for my $i (0 .. $#pairs) {
            next if $pairs[$i]{removed};
            if ($pairs[$i]{left} eq $conflict->{original} ||
                $pairs[$i]{right} eq $conflict->{original}) {
                $pairs[$i]{removed} = 1;
                $pairs[$i]{reason} = "Involved in unresolvable conflict";
                print "  -> Removing line $pairs[$i]{line}: '$pairs[$i]{left}' <-> '$pairs[$i]{right}'\n";
                last;
            }
        }
    }
}

# Final verification
print "\n=== Final Verification ===\n";
my ($forward_ref, $reverse_ref) = build_maps();
my @final_conflicts = find_conflicts($forward_ref, $reverse_ref);

if (@final_conflicts) {
    print "WARNING: Still have " . scalar(@final_conflicts) . " unresolved conflicts!\n";
    for my $conflict (@final_conflicts) {
        print "  $conflict->{type}: '$conflict->{original}'\n";
    }
} else {
    print "All conflicts resolved. Configuration is now reversible.\n";
}

# Write output
print "\n=== Writing output to $output_file ===\n";

open(my $out_fh, '>:raw', $output_file) or die "Cannot open $output_file for writing: $!\n";

print $out_fh "$lookbehind_out\n";
print $out_fh "$lookahead_out\n";

my $kept = 0;
my $removed = 0;

for my $p (@pairs) {
    if ($p->{removed}) {
        print "REMOVED line $p->{line}: '$p->{left}' <-> '$p->{right}' - $p->{reason}\n";
        $removed++;
    } else {
        # Encode back any special characters
        my $left = encode_escapes($p->{left});
        my $right = encode_escapes($p->{right});
        print $out_fh "$left\t$right\n";
        $kept++;
    }
}

close($out_fh);

print "\nSummary: Kept $kept pairs, removed $removed pairs\n";

# Helper to decode escape sequences like \xE0
sub decode_escapes {
    my $str = shift;
    $str =~ s/\\x([0-9A-Fa-f]{2})/chr(hex($1))/ge;
    $str =~ s/\\t/\t/g;
    $str =~ s/\\n/\n/g;
    $str =~ s/\\r/\r/g;
    $str =~ s/\\\\/\\/g;
    return $str;
}

# Helper to encode special characters back to escape sequences
sub encode_escapes {
    my $str = shift;
    # Only encode non-printable characters
    $str =~ s/([\x00-\x1f\x7f-\xff])/sprintf("\\x%02X", ord($1))/ge;
    return $str;
}
