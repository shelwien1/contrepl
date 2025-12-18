# Controlled Regexp Replacement Compression Method

## Overview

This document describes the **controlled regular expression replacement** compression method implemented in `repl2.cpp`, based on the preprocessing technique discussed in the [encode.su forum thread](https://encode.su/threads/3072-controlled-regexp-tools-for-HP).

## Core Concept

The method is a **multi-pass text preprocessing scheme** that improves compression by reducing lexical diversity (the number of distinct words/tokens) in text data. It works by:

1. **Forward Transformation**: Applying regex-based pattern replacements to normalize text (e.g., expanding contractions, normalizing spellings)
2. **Flag Generation**: Recording binary flags at each potential restoration point
3. **Backward Restoration**: Using flags to selectively restore original content during decompression

The key insight is that the flags themselves can be efficiently compressed because:
- They are binary (0/1)
- They occur at predictable positions (where patterns match)
- The surrounding context helps predict their values

## How It Works

### Configuration Format

Each config file specifies:
```
<lookbehind_pattern>     # Line 1: PCRE2 lookbehind assertion
<lookahead_pattern>      # Line 2: PCRE2 lookahead assertion
<from>\t<to>             # Line 3+: tab-separated replacement pairs
<from>\t<to>
...
```

Example (contraction expansion):
```
[^a-zA-Z']
[^a-zA-Z']
don't	do not
can't	cannot
I'm	I am
```

The lookbehind/lookahead patterns define word boundaries, ensuring replacements only occur at appropriate locations.

### Compression Pipeline

```
           +----------+     +----------------+     +-----------+
Original →| Forward  |→   | Generate Flags |→   | Compressed|
  Data    | Replace  |    | (compare orig) |    |    Data   |
           +----------+     +----------------+     +-----------+
                                   ↓
                            +------------+
                            |   Flags    |
                            | (encoded)  |
                            +------------+
```

1. **Forward Pass**: Apply all `from → to` replacements matching the context patterns
2. **Backward Scan**: Find all positions where `to → from` could apply
3. **Flag Generation**: For each backward match, compare against original:
   - Flag = 1 if restoration is needed (original had the `from` form)
   - Flag = 0 if current form should be kept (original had the `to` form)

### Decompression Pipeline

```
           +----------------+     +----------+
Compressed|                |    |          |
   Data   |  Read Flags &  |→   | Original |
     +    |  Restore Where |    |   Data   |
  Flags   |  Flag = 1      |    |          |
           +----------------+     +----------+
```

### Context-Based Flag Modeling

Flags are written with 32 bytes of context before and after each match position. This context enables the flag encoder (DLL module) to:
- Use order-N models for prediction
- Exploit patterns in where restorations occur
- Achieve better compression than raw flag storage

## Implementation Details (repl2.cpp)

### Key Components

| Component | Description |
|-----------|-------------|
| `ParsedConfig` | Holds lookbehind, lookahead patterns and replacement pairs |
| `compress_single()` | Forward transform + flag generation for one config |
| `decompress_single()` | Flag-guided restoration for one config |
| `API` function | DLL-based flag encoder/decoder with context modeling |

### Multi-Config Support

The tool supports:
- **Multi-config files**: Multiple configs in one file, separated by blank lines
- **List files**: `@listfile` syntax for a list of config file paths

Configs are applied in sequence during compression and reversed during decompression.

### Pattern Matching

- Uses PCRE2 library with JIT compilation for performance
- Builds alternation patterns sorted by length (longest first) to handle overlaps
- Lookbehind/lookahead assertions ensure proper word boundary matching

## Effectiveness

The method is most effective when:
1. **High redundancy** exists between variants (e.g., "don't" vs "do not")
2. **Predictable patterns** govern which variant appears (context-dependent)
3. **Flag compression** achieves better than 1 bit/flag through modeling

Example results from the forum:
- Contraction expansion on enwik8: ~39 bytes saved
- UTF-8 punctuation normalization: Mixed results depending on compressor

---

# Proposed New Configurations

The following 5 configurations are designed to reduce lexical diversity while preserving sentence structure and semantics. Each targets a specific type of variation in English text.

## Config 1: Article Normalization (a/an)

**Goal**: Normalize "an" to "a" everywhere, using flags to restore "an" before vowel sounds.

**Rationale**: "a" and "an" are determiners with identical meaning; the choice is purely phonetic. Merging them reduces the vocabulary while the flag can restore proper grammar.

```
[^a-zA-Z]
[^a-zA-Z]
an	a
An	A
AN	A
```

**File**: `config_articles.txt`

**Expected Impact**: Moderate - "an" appears frequently in English text. The flag should be highly predictable (nearly always before vowels), leading to good flag compression.

---

## Config 2: British/American Spelling Normalization

**Goal**: Normalize British spellings to American equivalents.

**Rationale**: Many English texts mix British and American spellings. Normalizing reduces distinct word forms while maintaining identical semantics.

```
[^a-zA-Z]
[^a-zA-Z]
colour	color
Colour	Color
COLOUR	COLOR
favour	favor
Favour	Favor
FAVOUR	FAVOR
honour	honor
Honour	Honor
HONOUR	HONOR
labour	labor
Labour	Labor
LABOUR	LABOR
behaviour	behavior
Behaviour	Behavior
BEHAVIOUR	BEHAVIOR
neighbour	neighbor
Neighbour	Neighbor
NEIGHBOUR	NEIGHBOR
favourite	favorite
Favourite	Favorite
FAVOURITE	FAVORITE
flavour	flavor
Flavour	Flavor
FLAVOUR	FLAVOR
humour	humor
Humour	Humor
HUMOUR	HUMOR
rumour	rumor
Rumour	Rumor
RUMOUR	RUMOR
centre	center
Centre	Center
CENTRE	CENTER
theatre	theater
Theatre	Theater
THEATRE	THEATER
metre	meter
Metre	Meter
METRE	METER
litre	liter
Litre	Liter
LITRE	LITER
fibre	fiber
Fibre	Fiber
FIBRE	FIBER
defence	defense
Defence	Defense
DEFENCE	DEFENSE
offence	offense
Offence	Offense
OFFENCE	OFFENSE
licence	license
Licence	License
LICENCE	LICENSE
practise	practice
Practise	Practice
PRACTISE	PRACTICE
analyse	analyze
Analyse	Analyze
ANALYSE	ANALYZE
organise	organize
Organise	Organize
ORGANISE	ORGANIZE
realise	realize
Realise	Realize
REALISE	REALIZE
recognise	recognize
Recognise	Recognize
RECOGNISE	RECOGNIZE
apologise	apologize
Apologise	Apologize
APOLOGISE	APOLOGIZE
criticise	criticize
Criticise	Criticize
CRITICISE	CRITICIZE
emphasise	emphasize
Emphasise	Emphasize
EMPHASISE	EMPHASIZE
summarise	summarize
Summarise	Summarize
SUMMARISE	SUMMARIZE
catalogue	catalog
Catalogue	Catalog
CATALOGUE	CATALOG
dialogue	dialog
Dialogue	Dialog
DIALOGUE	DIALOG
programme	program
Programme	Program
PROGRAMME	PROGRAM
grey	gray
Grey	Gray
GREY	GRAY
travelling	traveling
Travelling	Traveling
TRAVELLING	TRAVELING
cancelled	canceled
Cancelled	Canceled
CANCELLED	CANCELED
levelled	leveled
Levelled	Leveled
LEVELLED	LEVELED
modelling	modeling
Modelling	Modeling
MODELLING	MODELING
```

**File**: `config_british_american.txt`

**Expected Impact**: Variable - depends on source. Wikipedia and mixed-origin corpora benefit most.

---

## Config 3: Regular Plural Simplification

**Goal**: Reduce regular plural nouns to their singular form.

**Rationale**: Regular plurals (word + "s") double the vocabulary for nouns. The plural flag can be predicted from context (preceding numbers, "many", "some", etc.).

**Note**: This config uses carefully selected high-frequency nouns with regular plurals to avoid irregular forms and verb conflicts.

```
[^a-zA-Z]
[^a-zA-Z]
years	year
Years	Year
YEARS	YEAR
days	day
Days	Day
DAYS	DAY
times	time
Times	Time
TIMES	TIME
people	person
People	Person
PEOPLE	PERSON
things	thing
Things	Thing
THINGS	THING
children	child
Children	Child
CHILDREN	CHILD
ways	way
Ways	Way
WAYS	WAY
words	word
Words	Word
WORDS	WORD
books	book
Books	Book
BOOKS	BOOK
places	place
Places	Place
PLACES	PLACE
cases	case
Cases	Case
CASES	CASE
groups	group
Groups	Group
GROUPS	GROUP
problems	problem
Problems	Problem
PROBLEMS	PROBLEM
facts	fact
Facts	Fact
FACTS	FACT
months	month
Months	Month
MONTHS	MONTH
weeks	week
Weeks	Week
WEEKS	WEEK
hours	hour
Hours	Hour
HOURS	HOUR
minutes	minute
Minutes	Minute
MINUTES	MINUTE
points	point
Points	Point
POINTS	POINT
parts	part
Parts	Part
PARTS	PART
members	member
Members	Member
MEMBERS	MEMBER
areas	area
Areas	Area
AREAS	AREA
students	student
Students	Student
STUDENTS	STUDENT
companies	company
Companies	Company
COMPANIES	COMPANY
numbers	number
Numbers	Number
NUMBERS	NUMBER
systems	system
Systems	System
SYSTEMS	SYSTEM
programs	program
Programs	Program
PROGRAMS	PROGRAM
questions	question
Questions	Question
QUESTIONS	QUESTION
governments	government
Governments	Government
GOVERNMENTS	GOVERNMENT
countries	country
Countries	Country
COUNTRIES	COUNTRY
cities	city
Cities	City
CITIES	CITY
states	state
States	State
STATES	STATE
services	service
Services	Service
SERVICES	SERVICE
results	result
Results	Result
RESULTS	RESULT
```

**File**: `config_plurals.txt`

**Expected Impact**: High - plurals are very common. Context (quantifiers, verb agreement) should enable good flag prediction.

---

## Config 4: Common Verb Past Tense Normalization

**Goal**: Normalize regular past tense verbs (-ed) to present tense.

**Rationale**: Regular past tense doubles the vocabulary for verbs. Tense can often be predicted from temporal markers ("yesterday", "last week", auxiliary verbs).

**Note**: Only includes high-frequency regular verbs to avoid irregular forms.

```
[^a-zA-Z]
[^a-zA-Z]
called	call
Called	Call
CALLED	CALL
asked	ask
Asked	Ask
ASKED	ASK
used	use
Used	Use
USED	USE
worked	work
Worked	Work
WORKED	WORK
seemed	seem
Seemed	Seem
SEEMED	SEEM
looked	look
Looked	Look
LOOKED	LOOK
wanted	want
Wanted	Want
WANTED	WANT
needed	need
Needed	Need
NEEDED	NEED
started	start
Started	Start
STARTED	START
turned	turn
Turned	Turn
TURNED	TURN
helped	help
Helped	Help
HELPED	HELP
showed	show
Showed	Show
SHOWED	SHOW
played	play
Played	Play
PLAYED	PLAY
moved	move
Moved	Move
MOVED	MOVE
lived	live
Lived	Live
LIVED	LIVE
believed	believe
Believed	Believe
BELIEVED	BELIEVE
happened	happen
Happened	Happen
HAPPENED	HAPPEN
included	include
Included	Include
INCLUDED	INCLUDE
continued	continue
Continued	Continue
CONTINUED	CONTINUE
expected	expect
Expected	Expect
EXPECTED	EXPECT
appeared	appear
Appeared	Appear
APPEARED	APPEAR
reported	report
Reported	Report
REPORTED	REPORT
decided	decide
Decided	Decide
DECIDED	DECIDE
remained	remain
Remained	Remain
REMAINED	REMAIN
suggested	suggest
Suggested	Suggest
SUGGESTED	SUGGEST
required	require
Required	Require
REQUIRED	REQUIRE
considered	consider
Considered	Consider
CONSIDERED	CONSIDER
returned	return
Returned	Return
RETURNED	RETURN
received	receive
Received	Receive
RECEIVED	RECEIVE
allowed	allow
Allowed	Allow
ALLOWED	ALLOW
talked	talk
Talked	Talk
TALKED	TALK
walked	walk
Walked	Walk
WALKED	WALK
followed	follow
Followed	Follow
FOLLOWED	FOLLOW
created	create
Created	Create
CREATED	CREATE
opened	open
Opened	Open
OPENED	OPEN
```

**File**: `config_past_tense.txt`

**Expected Impact**: Moderate to high - depends on text genre. Narrative prose has many past tense verbs.

---

## Config 5: Unicode Punctuation to ASCII

**Goal**: Normalize Unicode punctuation to ASCII equivalents.

**Rationale**: Many texts contain Unicode variants of common punctuation (curly quotes, em-dashes, ellipsis). Normalizing to ASCII reduces character diversity and may improve compression of the main data stream.

```


\xe2\x80\x94	--
\xe2\x80\x93	--
\xe2\x80\x98	'
\xe2\x80\x99	'
\xe2\x80\x9c	"
\xe2\x80\x9d	"
\xe2\x80\xa6	...
\xc2\xab	"
\xc2\xbb	"
\xe2\x80\xb9	'
\xe2\x80\xba	'
\xe2\x80\x90	-
\xe2\x80\x91	-
\xe2\x80\x92	-
\xe2\x81\x83	-
\xc2\xa0
\xe2\x80\x89
\xe2\x80\x88
\xe2\x80\x87
\xe2\x80\x86
\xe2\x80\x85
\xe2\x80\x84
\xe2\x80\x83
\xe2\x80\x82
\xe2\x80\x81
\xe2\x80\x80
\xef\xbb\xbf
```

**Notes**:
- Lines 1-2 are empty (no lookbehind/lookahead constraints - match anywhere)
- Uses `\xNN` hex escapes for UTF-8 byte sequences
- Maps:
  - Em-dash (U+2014) and En-dash (U+2013) → `--`
  - Curly single quotes (U+2018, U+2019) → `'`
  - Curly double quotes (U+201C, U+201D) → `"`
  - Horizontal ellipsis (U+2026) → `...`
  - Guillemets (U+00AB, U+00BB) → `"`
  - Various space characters → regular space
  - BOM (U+FEFF) → empty

**File**: `config_unicode_punct.txt`

**Expected Impact**: Variable - depends heavily on source. Web-scraped content often has mixed punctuation styles.

---

## Summary Table

| Config | Target | Vocabulary Reduction | Flag Predictability |
|--------|--------|---------------------|---------------------|
| Articles | a/an | Low | Very High |
| British/American | Spelling variants | Medium | Document-level (clustered) |
| Plurals | Noun forms | High | Medium-High (quantifier context) |
| Past Tense | Verb forms | High | Medium (temporal context) |
| Unicode Punct | Character variants | Medium | Document-level (clustered) |

## Usage

Apply configs in a pipeline using list mode:

```bash
# Create list file
echo "config_articles.txt" > configs.lst
echo "config_british_american.txt" >> configs.lst
echo "config_plurals.txt" >> configs.lst
echo "config_past_tense.txt" >> configs.lst
echo "config_unicode_punct.txt" >> configs.lst

# Compress
./repl2 c @configs.lst input.txt output.txt flags.bin

# Decompress
./repl2 d @configs.lst output.txt restored.txt flags.bin
```

## Evaluation Considerations

When testing these configs:

1. **Measure total size**: transformed data + encoded flags
2. **Test with different compressors**: Some compressors benefit more from reduced vocabulary
3. **Consider text genre**: Scientific text benefits from spelling normalization; fiction benefits from tense normalization
4. **Watch for false positives**: Words that match patterns but shouldn't transform (e.g., "used" as adjective vs. past tense)
