# Controlled Regexp Replacement Compression Method

## Overview

This document describes the **controlled regular expression replacement** compression method implemented in `repl2.cpp`, based on the preprocessing technique discussed in the [encode.su forum thread](https://encode.su/threads/3072-controlled-regexp-tools-for-HP).

## Target Data

This method is designed for **large text corpora**, particularly:
- **enwik8/enwik9**: The Wikipedia text benchmarks (100MB/1GB of cleaned English Wikipedia)
- **enwik_text2** and similar filtered text extracts
- Literary works (e.g., the Calgary corpus `book1`)
- Any natural language text with rich vocabulary

## Core Concept

The method is a **multi-pass text preprocessing scheme** that improves compression by reducing lexical diversity (the number of distinct words/tokens) in text data. It works by:

1. **Forward Transformation**: Applying regex-based pattern replacements to normalize text
2. **Flag Generation**: Recording binary flags at each potential restoration point
3. **Backward Restoration**: Using flags to selectively restore original content during decompression

### Key Insight: Bidirectional Context for Flag Compression

Traditional context modeling (CM) in compression only has access to **left context** (bytes already processed). However, the repl2 API provides the flag encoder with **both left and right context** (32 bytes in each direction around each match). This is a significant advantage because:

- **Right context is normally inaccessible** to streaming compressors
- The flag encoder can exploit **bidirectional patterns** for better prediction
- Context like "the ___ dog" (where ___ is the match position) can use both "the" (left) and "dog" (right) to predict the flag

This enables flag compression that significantly exceeds what would be possible with standard CM approaches.

### Replacement Scope: Beyond Grammar

Replacements are **not limited to grammatical variations**. Any word can potentially be replaced by:

1. **Synonyms**: big → large, small → little, fast → quick
2. **Antonyms**: good → bad, hot → cold, up → down (they share similar syntactic contexts!)
3. **Frequency normalization**: Map less frequent words to more frequent words that appear in similar contexts
4. **Any contextually compatible word**: If two words can appear in the same contexts, one can be mapped to the other

The goal is to **minimize the vocabulary** of the transformed text. The flags, compressed with bidirectional context, restore the original words where needed.

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

### Context-Based Flag Modeling (Bidirectional CM)

Flags are written with **32 bytes of context before AND after** each match position. This bidirectional context enables the flag encoder (DLL module) to:

- Use **order-N models with right context** - normally impossible in streaming compression
- Exploit patterns on both sides: "the ___ is" tells us more than just "the ___"
- Use **SSE (Secondary Symbol Estimation)** with features from both directions
- Achieve significantly better compression than left-context-only approaches

**Why this matters**: Consider predicting whether "large" should be restored to "big":
- Left context only: "a ___ house" - limited information
- Bidirectional: "a ___ house with" - more context for prediction
- The right context often contains grammatical or semantic cues that strongly predict the flag

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

### Critical: Many-to-One Mappings Require Multi-Config

**Important constraint**: Within a single config, each replacement target (`to` value) can only map back to ONE source (`from` value). If multiple words map to the same target in one config, only the first can be restored - **this would be lossy**.

**Example of LOSSY config** (DO NOT USE):
```
[^a-zA-Z]
[^a-zA-Z]
stated	said
declared	said    # PROBLEM: "said" already maps back to "stated"
exclaimed	said   # PROBLEM: cannot restore to "exclaimed"
```

**Solution**: Use multi-config format. However, **replacements with different targets CAN be combined** in the same config to reduce file passes:

```
[^a-zA-Z]
[^a-zA-Z]
stated	said
Stated	Said
walked	went
Walked	Went
looked	saw
Looked	Saw

[^a-zA-Z]
[^a-zA-Z]
declared	said
Declared	Said
ran	went
Ran	Went
gazed	saw
Gazed	Saw

[^a-zA-Z]
[^a-zA-Z]
exclaimed	said
Exclaimed	Said
rushed	went
Rushed	Went
stared	saw
Stared	Saw
```

**Key principle**:
- **Same target** (e.g., stated→said, declared→said) → must be in **separate configs**
- **Different targets** (e.g., stated→said, walked→went) → can be in **same config**

This reduces the number of configs from 24 (one per word) to 9 (grouping by target collision), significantly reducing file passes while maintaining lossless operation.

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

---

# Additional Configurations: Semantic Replacements

The following 5 configurations exploit the fact that **semantically related words share syntactic contexts**. This includes synonyms, antonyms, and frequency-based substitutions. The bidirectional context available to the flag encoder makes these replacements particularly effective.

## Config 6: Common Synonym Pairs (Size/Magnitude)

**Goal**: Map less frequent size-related words to more frequent equivalents.

**Rationale**: Words like "big/large", "small/little" appear in identical syntactic contexts. Mapping to the more frequent variant reduces vocabulary; the flag (with bidirectional context) can predict which original word was used based on surrounding text style.

**Note**: Uses multi-config format - words mapping to "big" and "little" are combined per config since they have different targets. Words mapping to the same target are in separate configs.

```
[^a-zA-Z]
[^a-zA-Z]
large	big
Large	Big
LARGE	BIG
small	little
Small	Little
SMALL	LITTLE

[^a-zA-Z]
[^a-zA-Z]
huge	big
Huge	Big
HUGE	BIG
tiny	little
Tiny	Little
TINY	LITTLE

[^a-zA-Z]
[^a-zA-Z]
enormous	big
Enormous	Big
ENORMOUS	BIG
miniature	little
Miniature	Little
MINIATURE	LITTLE

[^a-zA-Z]
[^a-zA-Z]
vast	big
Vast	Big
VAST	BIG
minute	little
Minute	Little
MINUTE	LITTLE

[^a-zA-Z]
[^a-zA-Z]
immense	big
Immense	Big
IMMENSE	BIG

[^a-zA-Z]
[^a-zA-Z]
gigantic	big
Gigantic	Big
GIGANTIC	BIG

[^a-zA-Z]
[^a-zA-Z]
massive	big
Massive	Big
MASSIVE	BIG
```

**File**: `config_synonyms_size.txt`

**Expected Impact**: Moderate - size adjectives are common. The bidirectional context helps distinguish formal ("enormous") from informal ("big") writing styles.

---

## Config 7: Antonym Pairs (Opposites Share Contexts)

**Goal**: Map antonym pairs to a single canonical form.

**Rationale**: Antonyms like "good/bad", "hot/cold" share nearly identical syntactic environments ("the ___ weather", "a ___ idea"). The flag encodes which polarity was intended. This is counterintuitive but effective because the **syntactic context is the same**; only semantics differ.

```
[^a-zA-Z]
[^a-zA-Z]
bad	good
Bad	Good
BAD	GOOD
cold	hot
Cold	Hot
COLD	HOT
slow	fast
Slow	Fast
SLOW	FAST
weak	strong
Weak	Strong
WEAK	STRONG
dark	light
Dark	Light
DARK	LIGHT
old	new
Old	New
OLD	NEW
wrong	right
Wrong	Right
WRONG	RIGHT
poor	rich
Poor	Rich
POOR	RICH
short	long
Short	Long
SHORT	LONG
hard	soft
Hard	Soft
HARD	SOFT
empty	full
Empty	Full
EMPTY	FULL
dead	alive
Dead	Alive
DEAD	ALIVE
cheap	expensive
Cheap	Expensive
CHEAP	EXPENSIVE
dry	wet
Dry	Wet
DRY	WET
```

**File**: `config_antonyms.txt`

**Expected Impact**: Variable - depends on the semantic coherence of the text. Works best when antonyms appear in similar frequencies. The flag essentially encodes the "polarity" of the adjective.

---

## Config 8: Motion/Action Verb Synonyms

**Goal**: Normalize motion and action verbs to common base forms.

**Rationale**: Verbs like "walk/stroll/march" or "say/state/declare" share syntactic contexts. Mapping to the most frequent variant reduces vocabulary significantly in narrative text.

**Note**: Uses multi-config format. Words mapping to different targets (said, went, saw) are combined in each config. Words mapping to the same target are split across configs.

```
[^a-zA-Z]
[^a-zA-Z]
stated	said
Stated	Said
STATED	SAID
walked	went
Walked	Went
WALKED	WENT
looked	saw
Looked	Saw
LOOKED	SAW

[^a-zA-Z]
[^a-zA-Z]
declared	said
Declared	Said
DECLARED	SAID
ran	went
Ran	Went
RAN	WENT
gazed	saw
Gazed	Saw
GAZED	SAW

[^a-zA-Z]
[^a-zA-Z]
exclaimed	said
Exclaimed	Said
EXCLAIMED	SAID
rushed	went
Rushed	Went
RUSHED	WENT
stared	saw
Stared	Saw
STARED	SAW

[^a-zA-Z]
[^a-zA-Z]
replied	said
Replied	Said
REPLIED	SAID
hurried	went
Hurried	Went
HURRIED	WENT
glanced	saw
Glanced	Saw
GLANCED	SAW

[^a-zA-Z]
[^a-zA-Z]
answered	said
Answered	Said
ANSWERED	SAID
strolled	went
Strolled	Went
STROLLED	WENT
watched	saw
Watched	Saw
WATCHED	SAW

[^a-zA-Z]
[^a-zA-Z]
remarked	said
Remarked	Said
REMARKED	SAID
marched	went
Marched	Went
MARCHED	WENT
observed	saw
Observed	Saw
OBSERVED	SAW

[^a-zA-Z]
[^a-zA-Z]
whispered	said
Whispered	Said
WHISPERED	SAID
dashed	went
Dashed	Went
DASHED	WENT

[^a-zA-Z]
[^a-zA-Z]
shouted	said
Shouted	Said
SHOUTED	SAID
sprinted	went
Sprinted	Went
SPRINTED	WENT

[^a-zA-Z]
[^a-zA-Z]
muttered	said
Muttered	Said
MUTTERED	SAID
```

**File**: `config_verb_synonyms.txt`

**Expected Impact**: High for narrative/literary text. Speech verbs ("said" variants) are especially common in fiction. The right context (quotation marks, dialogue patterns) helps predict the original verb.

---

## Config 9: Common Word Frequency Normalization

**Goal**: Replace less frequent common words with their more frequent near-synonyms.

**Rationale**: Many common words have frequency disparities despite similar usage. Mapping to the more frequent variant improves compression of the main text; flags restore the less frequent originals.

```
[^a-zA-Z]
[^a-zA-Z]
although	though
Although	Though
ALTHOUGH	THOUGH
whilst	while
Whilst	While
WHILST	WHILE
amongst	among
Amongst	Among
AMONGST	AMONG
towards	toward
Towards	Toward
TOWARDS	TOWARD
upon	on
Upon	On
UPON	ON
also	too
Also	Too
ALSO	TOO
however	but
However	But
HOWEVER	BUT
therefore	so
Therefore	So
THEREFORE	SO
perhaps	maybe
Perhaps	Maybe
PERHAPS	MAYBE
certainly	sure
Certainly	Sure
CERTAINLY	SURE
nearly	almost
Nearly	Almost
NEARLY	ALMOST
quite	very
Quite	Very
QUITE	VERY
rather	quite
Rather	Quite
RATHER	QUITE
often	much
Often	Much
OFTEN	MUCH
always	ever
Always	Ever
ALWAYS	EVER
```

**File**: `config_frequency_normalize.txt`

**Expected Impact**: Moderate - these words are common but the mappings are less semantically tight. Best for mixed-register text (Wikipedia, web content).

---

## Config 10: Quality/Descriptive Adjective Synonyms

**Goal**: Normalize descriptive adjectives to common base forms.

**Rationale**: Adjectives like "beautiful/pretty/lovely" or "important/significant/crucial" share contexts. Normalizing reduces vocabulary in descriptive text.

**Note**: Uses multi-config format. Each config combines one word from each target group (nice, great, bad, key, hard, easy) to minimize passes while avoiding target collisions.

```
[^a-zA-Z]
[^a-zA-Z]
beautiful	nice
Beautiful	Nice
BEAUTIFUL	NICE
wonderful	great
Wonderful	Great
WONDERFUL	GREAT
terrible	bad
Terrible	Bad
TERRIBLE	BAD
important	key
Important	Key
IMPORTANT	KEY
difficult	hard
Difficult	Hard
DIFFICULT	HARD
simple	easy
Simple	Easy
SIMPLE	EASY

[^a-zA-Z]
[^a-zA-Z]
pretty	nice
Pretty	Nice
PRETTY	NICE
excellent	great
Excellent	Great
EXCELLENT	GREAT
awful	bad
Awful	Bad
AWFUL	BAD
significant	key
Significant	Key
SIGNIFICANT	KEY
challenging	hard
Challenging	Hard
CHALLENGING	HARD
straightforward	easy
Straightforward	Easy
STRAIGHTFORWARD	EASY

[^a-zA-Z]
[^a-zA-Z]
lovely	nice
Lovely	Nice
LOVELY	NICE
fantastic	great
Fantastic	Great
FANTASTIC	GREAT
horrible	bad
Horrible	Bad
HORRIBLE	BAD
crucial	key
Crucial	Key
CRUCIAL	KEY

[^a-zA-Z]
[^a-zA-Z]
amazing	great
Amazing	Great
AMAZING	GREAT
dreadful	bad
Dreadful	Bad
DREADFUL	BAD
essential	key
Essential	Key
ESSENTIAL	KEY

[^a-zA-Z]
[^a-zA-Z]
vital	key
Vital	Key
VITAL	KEY

[^a-zA-Z]
[^a-zA-Z]
major	key
Major	Key
MAJOR	KEY
```

**File**: `config_adjective_synonyms.txt`

**Expected Impact**: High for descriptive/evaluative text (reviews, articles). The bidirectional context captures the register and formality level to predict the original word.

---

## Extended Summary Table

| Config | Target | Vocabulary Reduction | Flag Predictability |
|--------|--------|---------------------|---------------------|
| Articles | a/an | Low | Very High |
| British/American | Spelling variants | Medium | Document-level |
| Plurals | Noun forms | High | Medium-High |
| Past Tense | Verb forms | High | Medium |
| Unicode Punct | Character variants | Medium | Document-level |
| **Synonyms (Size)** | Size adjectives | Medium | Medium (style-based) |
| **Antonyms** | Opposite pairs | Medium | Medium (semantic) |
| **Verb Synonyms** | Motion/speech verbs | High | High (dialogue context) |
| **Frequency Norm** | Common words | Medium | Medium |
| **Adjective Synonyms** | Quality adjectives | High | Medium-High (register) |

## Evaluation Considerations

When testing these configs:

1. **Measure total size**: transformed data + encoded flags
2. **Test with different compressors**: Some compressors benefit more from reduced vocabulary
3. **Consider text genre**: Scientific text benefits from spelling normalization; fiction benefits from tense normalization
4. **Watch for false positives**: Words that match patterns but shouldn't transform (e.g., "used" as adjective vs. past tense)
5. **Semantic configs require good flag compression**: Antonym/synonym replacements only help if the bidirectional CM can predict flags well
6. **Order matters**: Apply grammatical configs before semantic configs; the transformed text provides better context for semantic flag prediction

---

# Experimental Results

## Test Setup

- **Input**: `enwik_text2` (881,305,418 bytes) - cleaned Wikipedia text
- **Preprocessed**: `enwik_text2_drt` (560,405,219 bytes) - after DRT preprocessing
- **Compressors tested**:
  - **fp8**: Fast PAQ variant (baseline: 140,389,379 bytes)
  - **hpc**: High-performance compressor (baseline: 125,355,714 bytes)

## Results Table

| Config | Config File | Transformed Size | Flag Size | fp8 Compressed | fp8 Gain | hpc Compressed | hpc Gain |
|--------|-------------|------------------|-----------|----------------|----------|----------------|----------|
| Baseline | - | 560,405,219 | - | 140,389,379 | - | 125,355,714 | - |
| 0 | htmlc.txt | 560,115,260 | 20,404 | 140,330,700 | **+38,275** | 125,299,185 | **+36,125** |
| 1 | config_british_american.txt | 560,390,130 | 16,962 | 140,363,390 | **+9,027** | 125,331,822 | **+6,930** |
| 2 | config_adjective_synonyms.txt | 560,398,902 | 33,360 | 140,365,954 | -9,935 | 125,335,069 | -12,715 |
| 3 | config_antonyms.txt | 560,422,927 | 31,281 | 140,361,692 | -3,594 | 125,330,831 | -6,394 |
| 4 | config_frequency_normalize.txt | 560,590,302 | 69,755 | 140,336,990 | -17,366 | 125,310,683 | -24,724 |
| 5 | config_past_tense.txt | 560,421,707 | 47,927 | 140,369,053 | -27,601 | 125,340,806 | -33,019 |
| 6 | config_plurals.txt | 560,405,219 | 110,140 | 140,296,634 | -17,395 | 125,289,481 | -43,907 |
| 7 | config_synonyms_size.txt | 560,397,805 | 27,021 | 140,367,079 | -4,721 | 125,337,009 | -8,316 |
| 8 | config_verb_synonyms.txt | 560,394,948 | 17,042 | 140,376,548 | -4,211 | 125,345,615 | -6,943 |

**Note**: Positive gain = compression improvement (bytes saved). Negative = overhead exceeds benefit.

## Analysis

### Successful Configs

**Config 0 (htmlc.txt)**: Best performer with +38KB gain on fp8. This config targets HTML/markup-specific patterns where replacements are highly predictable from context.

**Config 1 (British/American spelling)**: Modest but consistent gains (+9KB fp8, +7KB hpc). This works because:
- Spelling variants are **document-level consistent** (a document uses either British OR American spelling throughout)
- Flag prediction is nearly perfect once the document's spelling style is detected
- Low flag count (16,962) indicates few actual transformations needed

### Unsuccessful Configs

All semantic replacement configs showed **negative results**:

| Config | Flag Overhead | Main Issue |
|--------|---------------|------------|
| Plurals | 110,140 | Highest flag count - too many transformations |
| Frequency Normalize | 69,755 | Words like "upon"→"on" don't share enough context |
| Past Tense | 47,927 | Tense prediction harder than expected |
| Adjective Synonyms | 33,360 | Style/register not predictable enough |
| Antonyms | 31,281 | Semantic polarity unpredictable from syntax |
| Synonyms Size | 27,021 | Formal/informal distinction too subtle |
| Verb Synonyms | 17,042 | Speech verbs vary by author style, not context |

### Key Insights

1. **Flag overhead is critical**: The flag file must compress well enough to offset the vocabulary reduction benefit. For semantic replacements, the flags are too unpredictable.

2. **Document-level consistency wins**: British/American spelling works because the "choice" is made once per document, not per word. The flag encoder can learn this quickly.

3. **Syntactic context ≠ semantic predictability**: While antonyms like "good/bad" share syntactic contexts ("a ___ idea"), the semantic choice cannot be predicted from syntax alone.

4. **High transformation count hurts**: Plurals had the most transformations (110K flags) but also the worst hpc result (-44KB). More flags = more unpredictable bits to encode.

5. **Compressor matters**: hpc consistently shows larger losses than fp8, suggesting the vocabulary reduction benefit is already captured by hpc's stronger modeling.

### Recommendations

For practical use:

1. **Use sparingly**: Only apply configs with proven positive results (spelling normalization, HTML/markup patterns)

2. **Profile first**: Test each config individually on your target corpus before combining

3. **Avoid semantic replacements**: Synonym/antonym/tense configs don't work well on Wikipedia-style text

4. **Consider corpus characteristics**: Fiction with consistent narrator style might benefit more from verb synonym configs than encyclopedic text

5. **Stack successful configs**: Combine only configs that individually show gains; losses tend to compound

