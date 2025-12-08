# mode_compress() Hotspot Analysis

## Overview

`mode_compress()` performs two main phases:
1. **Forward replacement**: Apply replacements from `original` → `intermediate`
2. **Backward simulation**: Simulate reverse replacements on `intermediate` to generate flags

## Identified Hotspots

### 1. `simulated.replace()` - **CRITICAL**

**Location**: Line 357
```cpp
simulated.replace(sim_pos, match_len, repl);
```

**Problem**: `string::replace()` is O(n) when replacement length differs from match length. It shifts all characters after the replacement point. With many replacements, this becomes O(n*m) where n=string length, m=number of replacements.

**Impact**: High - called for every successful backward replacement.

**Potential optimizations**:
- Build result incrementally instead of modifying in-place
- Use rope data structure for efficient insertions
- Pre-calculate final positions and build in single pass
- Since we're only checking positions against `original`, consider if we can avoid modifying `simulated` at all

---

### 2. PCRE2 Regex Matching - **HIGH**

**Locations**: Lines 270, 327
```cpp
rc = pcre2_match(fwd_re, (PCRE2_SPTR)original.c_str(), ...);
rc = pcre2_match(bwd_re, (PCRE2_SPTR)simulated.c_str(), ...);
```

**Problem**:
- Lookahead/lookbehind patterns `(?<=lb)...(? la)` are expensive
- Called once per character position in worst case
- JIT is enabled but pattern complexity limits gains

**Impact**: High - dominates runtime for large files.

**Potential optimizations**:
- Use `pcre2_jit_match()` instead of `pcre2_match()` for JIT-compiled patterns
- Pre-scan for `lb` anchors to skip regions without potential matches
- Consider Aho-Corasick or similar multi-pattern matcher for the alternation
- If `lb`/`la` are simple literals, use `memmem()` to find candidate positions first

---

### 3. String Concatenation in Loops - **MEDIUM**

**Locations**: Lines 282, 288, 298, 349
```cpp
intermediate += original.substr(last_end, start - last_end);
intermediate += repl;
flags += should ? '1' : '0';
```

**Problem**:
- `substr()` creates temporary string (though small overhead with SSO)
- `flags +=` char-by-char appending may cause reallocations

**Impact**: Medium - mitigated by `reserve()` but still creates temporaries.

**Potential optimizations**:
- Use `intermediate.append(original.data() + last_end, start - last_end)` instead of `substr()`
- Pre-calculate flags count and reserve exact size
- Use `flags.push_back()` with pre-reserved vector<char> instead of string

---

### 4. `vector<bool>` for `seen_sim_pos` - **MEDIUM**

**Location**: Line 324
```cpp
vector<bool> seen_sim_pos(simulated.length(), false);
```

**Problem**: `vector<bool>` is a bitfield with non-standard iterator behavior. Access may be slower than `vector<char>` due to bit manipulation.

**Impact**: Medium - accessed in tight loop.

**Potential optimizations**:
- Use `vector<char>` or `vector<uint8_t>` for faster access
- Use raw `new char[]` array for minimal overhead

---

### 5. Hash Map Lookups - **LOW-MEDIUM**

**Locations**: Lines 287, 339
```cpp
string_view repl = forward[match_str];
string_view repl = backward[match_str];
```

**Problem**: Hash computation on every lookup. `unordered_map` has good average case but hash collisions possible.

**Impact**: Low-Medium - already using `string_view` keys.

**Potential optimizations**:
- If keys are small and few, consider perfect hashing
- Cache hash values if same keys looked up repeatedly
- For small key sets, sorted vector + binary search may beat hash map

---

### 6. `build_alternation()` Sorting - **LOW**

**Location**: Lines 179-190
```cpp
// Sort by length descending - O(n^2) bubble sort
for (i = 0; i < sorted.size(); i++) {
  for (j = i + 1; j < sorted.size(); j++) { ... }
}
```

**Problem**: O(n^2) selection sort instead of O(n log n).

**Impact**: Low - only called twice during setup, not in hot path.

**Potential optimizations**:
- Use `std::sort()` with custom comparator

---

## Algorithmic Improvements

### A. Eliminate Backward Simulation String Modification

The backward simulation modifies `simulated` string but only uses positions to check against `original`. Since `original[0..sim_pos] == simulated[0..sim_pos]` is invariant, we might be able to:

1. Track a "virtual offset" instead of actually modifying the string
2. Compute what the position would be in `original` directly

**Complexity**: Would require careful bookkeeping of length changes.

---

### B. Two-Pass Approach

Instead of regex matching with modification:

1. **Pass 1**: Find all backward match positions in `intermediate` (no modification)
2. **Pass 2**: Process matches in order, computing flags based on cumulative offset

This eliminates the O(n) `string::replace()` calls entirely.

---

### C. Direct Pattern Matching

Replace PCRE2 alternation with:
- Aho-Corasick automaton for multi-pattern matching
- Then verify lookbehind/lookahead manually

This could be faster for large alternations with many patterns.

---

### D. Parallel Processing

For very large files:
- Split into chunks at safe boundaries (outside any match context)
- Process chunks in parallel
- Merge results

---

## Priority Ranking

| Priority | Hotspot | Expected Speedup | Effort |
|----------|---------|------------------|--------|
| 1 | simulated.replace() | 2-10x | Medium |
| 2 | PCRE2 matching strategy | 1.5-3x | High |
| 3 | String concatenation | 1.1-1.3x | Low |
| 4 | vector<bool> → vector<char> | 1.05-1.1x | Low |
| 5 | build_alternation sort | Negligible | Low |

## Quick Wins (Low Effort)

1. Replace `vector<bool>` with `vector<char>`
2. Use `std::sort()` in `build_alternation()`
3. Use `.append()` instead of `+= substr()`
4. Reserve `flags` string capacity upfront
5. Use `pcre2_jit_match()` instead of `pcre2_match()`
