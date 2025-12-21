# Structured Codes and Identifiers in Wikipedia (enwik9)

When parsing **enwik9** (the first 1 GB of English Wikipedia text), you encounter raw article content containing human-written text *and* numerous **structured identifiers** embedded in citations, templates, infoboxes, and lists. Beyond ISBNs, many patterned index codes can be exploited **to improve compression or build targeted token models**.

## 🔐 Verification & Check Digit Summary

Many identifiers include check digits or verification mechanisms that allow validation without external lookup. This is valuable for compression because:
- Invalid codes can be filtered out (reducing false positives)
- Check digit algorithms are predictable (can be computed, not stored)
- Verified codes have higher confidence for special token treatment

| Code | Check Mechanism | Algorithm | Verifiable? |
|------|----------------|-----------|-------------|
| **ISBN-10** | Last digit | Mod 11 weighted sum | ✅ Yes |
| **ISBN-13** | Last digit | Mod 10 (Luhn variant) | ✅ Yes |
| **ISSN** | Last digit | Mod 11 weighted sum | ✅ Yes |
| **ISMN** | Last digit | Mod 10 | ✅ Yes |
| **EAN-13** | Last digit | Mod 10 (Luhn variant) | ✅ Yes |
| **UPC-A** | Last digit | Mod 10 | ✅ Yes |
| **GTIN** | Last digit | Mod 10 | ✅ Yes |
| **ISIN** | Last digit | Luhn on alphanumeric | ✅ Yes |
| **CUSIP** | Last digit | Mod 10 weighted | ✅ Yes |
| **SEDOL** | Last digit | Weighted mod 10 | ✅ Yes |
| **IBAN** | Digits 3-4 | Mod 97 on rearranged | ✅ Yes |
| **VIN** | Position 9 | Transliteration + weights | ✅ Yes |
| **IMO** | Last digit | Weighted sum | ✅ Yes |
| **CAS RN** | Last digit | Weighted mod 10 | ✅ Yes |
| **ORCID** | Last char | Mod 11-2 (ISO 7064) | ✅ Yes |
| **ISNI** | Last char | Mod 11-2 (ISO 7064) | ✅ Yes |
| **DOI** | None | No check digit | ❌ No |
| **PMID** | None | No check digit | ❌ No |
| **arXiv** | None | No check digit | ❌ No |
| **UUID** | Version field | Version/variant bits | ⚠️ Partial |
| **IPv4** | None | Range check only (0-255) | ⚠️ Partial |
| **Email** | None | Syntax only | ⚠️ Partial |

## 📚 Publication & Bibliographic Identifiers

Extremely common due to Wikipedia's citation-heavy nature:

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ISBN-10** | Book number (old) | 9 digits + check | `0-306-40615-2` | ✅ Mod 11 |
| **ISBN-13** | Book number (new) | 12 digits + check | `978-0-306-40615-7` | ✅ Mod 10 |
| **ISSN** | Serial number | 7 digits + check | `0378-5955` | ✅ Mod 11 |
| **DOI** | Digital Object ID | `10.` + registrant + `/` + suffix | `10.1000/xyz123` | ❌ None |
| **PMID** | PubMed article ID | Integer (1-8 digits) | `12345678` | ❌ None |
| **PMCID** | PubMed Central ID | `PMC` + digits | `PMC1234567` | ❌ None |
| **arXiv** | Preprint ID | `arXiv:` + YYMM.NNNNN(v#) | `arXiv:2301.12345v2` | ❌ None |
| **CODEN** | Publication code | 6 alphanumeric | `NATUAS` | ❌ None |
| **SICI** | Serial Item ID | Complex pattern | `0095-4403(199502/03)...` | ✅ Check char |
| **BICI** | Book Item ID | Similar to SICI | | ✅ Check char |
| **PII** | Publisher Item ID | S/B prefix + alphanumeric | `S0002-9343(98)00120-0` | ❌ None |
| **Bibcode** | NASA ADS code | 19 chars (YYYYJJJJJVVVVMPPPPA) | `2009ApJ...696.1798T` | ❌ None |
| **OCLC** | WorldCat control # | Integer (6-14 digits) | `12345678` | ❌ None |
| **LCCN** | Library of Congress | Alphanumeric (8-12 chars) | `2001012345` | ❌ None |

## 🔗 Persistent & Authority Identifiers

Common in citations and authority control sections:

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ORCID** | Researcher ID | 16 digits (4x4) | `0000-0002-1825-0097` | ✅ Mod 11-2 |
| **Wikidata QID** | Entity ID | `Q` + digits | `Q42` | ❌ None |
| **VIAF** | Authority file | Integer | `29528720` | ❌ None |
| **ISNI** | Name identifier | 16 digits | `0000 0001 2281 955X` | ✅ Mod 11-2 |
| **ROR** | Research Org ID | `0` + 6 alphanum + 2 digits | `03yrm5c26` | ❌ None |
| **GND** | German library ID | Alphanumeric | `118650130` | ✅ Mod 11 |
| **FAST** | Subject terms | Integer | `1234567` | ❌ None |
| **NLA** | Australian library | Integer | `35023736` | ❌ None |
| **BNF** | French library | `cb` + digits + check | `cb119058202` | ✅ Check char |
| **SUDOC** | French univ docs | 8-9 digits | `026403188` | ❌ None |
| **NDL** | Japan library | Integer | `00620903` | ❌ None |
| **Open Library** | Open Library ID | `OL` + digits + `A/M/W` | `OL123456A` | ❌ None |

## 📦 Product, Reference & Catalog Codes

Less frequent but present in product/media articles:

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **UPC-A** | US/Canada barcode | 12 digits | `012345678905` | ✅ Mod 10 |
| **EAN-13** | International barcode | 13 digits | `5901234123457` | ✅ Mod 10 |
| **EAN-8** | Short barcode | 8 digits | `96385074` | ✅ Mod 10 |
| **GTIN** | Global Trade Item | 8/12/13/14 digits | | ✅ Mod 10 |
| **ASIN** | Amazon ID | 10 alphanumeric | `B08N5WRWNW` | ❌ None |
| **SKU** | Stock unit | Variable | | ❌ None |
| **MPN** | Manufacturer part | Variable | | ❌ None |

## 🌍 Geographic & Location Codes

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ISO 3166-1 α2** | Country codes | 2 letters | `US`, `GB` | ❌ Lookup only |
| **ISO 3166-1 α3** | Country codes | 3 letters | `USA`, `GBR` | ❌ Lookup only |
| **ISO 3166-1 num** | Country codes | 3 digits | `840`, `826` | ❌ Lookup only |
| **ISO 3166-2** | Subdivision | CC-SSS | `US-CA`, `GB-ENG` | ❌ Lookup only |
| **FIPS** | US geographic | 2+3 or 2+5 digits | `06037` | ❌ None |
| **GNIS** | US place names | Integer | `1660832` | ❌ None |
| **GeoNames** | Global places | Integer | `5128581` | ❌ None |
| **UN/LOCODE** | Locations | 5 chars (CC + 3) | `USLAX` | ❌ Lookup only |
| **IATA** | Airport codes | 3 letters | `LAX`, `JFK` | ❌ Lookup only |
| **ICAO** | Aviation codes | 4 letters | `KLAX`, `EGLL` | ❌ Lookup only |
| **Coordinates** | Lat/Long | Various | `40°42′46″N` | ⚠️ Range check |
| **US ZIP** | Postal code | 5 or 9 digits | `90210-1234` | ❌ None |
| **UK Postcode** | Postal code | Alphanumeric | `SW1A 1AA` | ⚠️ Format only |

## 🏛️ Government & Tax Identifiers

| Code | Description | Format | Country | Check? |
|------|-------------|--------|---------|--------|
| **SSN** | Social Security | NNN-NN-NNNN | US | ⚠️ Area rules |
| **EIN** | Employer ID | NN-NNNNNNN | US | ❌ None |
| **TIN** | Tax ID | Variable | Various | Varies |
| **VAT** | Value Added Tax | CC + up to 12 | EU | ✅ Per country |
| **ABN** | Australian Business | 11 digits | AU | ✅ Mod 89 |
| **SIREN** | French business | 9 digits | FR | ✅ Luhn |
| **SIRET** | French establishment | 14 digits | FR | ✅ Luhn |
| **Companies House** | UK company | 8 chars | UK | ❌ None |
| **D-U-N-S** | Business ID | 9 digits | Intl | ✅ Mod 10 |
| **LEI** | Legal Entity ID | 20 alphanumeric | Intl | ✅ Mod 97-10 |

## 🛂 Personal & Travel Identifiers

| Code | Description | Format | Notes | Check? |
|------|-------------|--------|-------|--------|
| **Passport** | Country-specific | Varies | Rarely in Wikipedia | Varies |
| **National ID** | Per country | Variable | DNI, NIE, Aadhaar | Varies |
| **Driver License** | State/country | Variable | | Varies |

## 📞 Communication Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **E.164** | Phone number | `+` + CC + number | `+1-555-123-4567` | ⚠️ Length only |
| **IPv4** | IP address | N.N.N.N (0-255) | `192.168.1.1` | ⚠️ Range check |
| **IPv6** | IP address | 8×4 hex | `2001:0db8::1` | ⚠️ Format only |
| **MAC Address** | Hardware addr | XX:XX:XX:XX:XX:XX | `00:1A:2B:3C:4D:5E` | ❌ None |
| **Email** | Email address | local@domain | `user@example.com` | ⚠️ Syntax only |
| **URL** | Web address | scheme://host/path | `https://example.com` | ⚠️ Syntax only |
| **URN** | Resource name | `urn:` + NID + NSS | `urn:isbn:0451450523` | ❌ None |

## 🚗 Vehicle & Transport Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **VIN** | Vehicle ID | 17 alphanumeric | `1HGBH41JXMN109186` | ✅ Position 9 |
| **IMO** | Ship ID | `IMO` + 7 digits | `IMO 9074729` | ✅ Weighted sum |
| **MMSI** | Maritime Mobile | 9 digits | `123456789` | ❌ None |
| **ICAO Aircraft** | Registration | Variable | `N12345` | ❌ None |
| **UIC Wagon** | Railway code | 12 digits | | ✅ Mod 10 |
| **Flight Number** | Airline + # | 2-3 + 1-4 digits | `AA123` | ❌ None |
| **License Plate** | Registration | Country-specific | | ❌ None |

## 🎬 Media & Entertainment Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ISAN** | Audiovisual work | 24 hex + checks | `0000-0000-3F3B-0000-0-...` | ✅ 2 check chars |
| **EIDR** | Entertainment ID | `10.5240/` + suffix | | ✅ ISO 7064 |
| **MusicBrainz** | Music DB ID | UUID | `f27ec8db-af05-...` | ⚠️ UUID format |
| **Discogs** | Music DB ID | Integer | | ❌ None |
| **IMDb** | Movie/TV ID | `tt/nm/co` + 7+ digits | `tt0111161` | ❌ None |
| **ISRC** | Recording code | CC-XXX-YY-NNNNN | `USRC17607839` | ❌ None |
| **ISWC** | Musical work | T-NNN.NNN.NNN-C | `T-010.113.674-6` | ✅ Mod 10 |
| **UPC (Music)** | Album barcode | 12 digits | | ✅ Mod 10 |

## 🔬 Scientific & Technical Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **CAS RN** | Chemical ID | N-NN-N (variable) | `7732-18-5` | ✅ Mod 10 |
| **SMILES** | Chemical struct | ASCII string | `CCO` | ⚠️ Syntax only |
| **InChI** | Chemical ID | `InChI=` + layers | | ⚠️ Syntax only |
| **InChIKey** | Hashed InChI | 27 chars | `LFQSCWFLJHTTHZ-UH...` | ⚠️ Format only |
| **PDB** | Protein DB | 4 alphanumeric | `1CRN` | ❌ None |
| **UniProt** | Protein DB | 6-10 alphanumeric | `P12345` | ❌ None |
| **GenBank** | Nucleotide seq | 1-2 letters + 5-6 digits | `AB123456` | ❌ None |
| **RefSeq** | NCBI reference | 2 letters + `_` + digits | `NM_001234567` | ❌ None |
| **ChEBI** | Chemical entity | `CHEBI:` + digits | `CHEBI:15377` | ❌ None |
| **EC Number** | Enzyme class | N.N.N.N | `1.1.1.1` | ❌ None |
| **ATC Code** | Drug class | L-NN-LL-NN | `A10BA02` | ❌ None |

## 💰 Financial Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ISIN** | Securities ID | 12 alphanumeric | `US0378331005` | ✅ Luhn |
| **CUSIP** | US/CA securities | 9 alphanumeric | `037833100` | ✅ Mod 10 |
| **SEDOL** | UK securities | 7 alphanumeric | `B0YBKJ7` | ✅ Weighted |
| **WKN** | German securities | 6 alphanumeric | `850206` | ❌ None |
| **Ticker** | Stock symbol | 1-5 letters | `AAPL` | ❌ Lookup only |
| **SWIFT/BIC** | Bank ID | 8 or 11 chars | `CHASUS33` | ❌ Lookup only |
| **IBAN** | Bank account | CC + 2 check + BBAN | `GB82WEST1234569876` | ✅ Mod 97 |
| **Routing #** | US bank routing | 9 digits | `021000021` | ✅ Weighted |

## 📅 Temporal Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **ISO 8601** | Date/time | YYYY-MM-DDTHH:MM:SS | `2024-01-15T14:30:00Z` | ⚠️ Range check |
| **Unix Timestamp** | Epoch seconds | Integer | `1705329000` | ❌ None |
| **Julian Date** | Astronomical | Decimal | `2460325.5` | ❌ None |
| **ISO Week** | Week number | YYYY-Wnn | `2024-W03` | ⚠️ Range check |

## 📋 Document & Legal Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **Patent (US)** | Patent # | `US` + 7-10 digits | `US7654321` | ❌ None |
| **Patent (EP)** | European | `EP` + 7-10 digits | `EP1234567` | ❌ None |
| **Patent (WO)** | International | `WO` + year + 6 digits | `WO2024/123456` | ❌ None |
| **Court Case** | Legal citation | Variable | `123 F.3d 456` | ❌ None |
| **CFR Citation** | Fed regulations | Title + CFR + Part | `21 CFR 820` | ❌ None |
| **ECLI** | EU Case Law | `ECLI:` + components | | ⚠️ Format only |

## 🔑 Technical & Software Identifiers

| Code | Description | Format | Example | Check? |
|------|-------------|--------|---------|--------|
| **UUID** | Unique ID | 8-4-4-4-12 hex | `550e8400-e29b-...` | ⚠️ Version bits |
| **Git SHA** | Commit hash | 40 hex chars | | ❌ None |
| **MD5** | Hash | 32 hex chars | | ❌ None |
| **SHA-1** | Hash | 40 hex chars | | ❌ None |
| **SHA-256** | Hash | 64 hex chars | | ❌ None |
| **CVE** | Vulnerability | `CVE-YYYY-NNNNN` | `CVE-2024-12345` | ❌ None |
| **CWE** | Weakness ID | `CWE-` + digits | `CWE-79` | ❌ None |
| **RFC** | Standards doc | `RFC ` + digits | `RFC 2616` | ❌ None |
| **Unicode** | Char code | `U+` + 4-6 hex | `U+00E9` | ⚠️ Range check |

---

## 🧮 Check Digit Algorithms Reference

### Mod 10 (Luhn) - Used by: EAN, UPC, GTIN, ISIN, Credit Cards
```
1. From right, double every 2nd digit
2. Sum all digits (split doubles >9 into individual digits)
3. Check digit makes total ≡ 0 (mod 10)
```

### Mod 11 - Used by: ISBN-10, ISSN, ORCID, ISNI
```
1. Multiply digits by weights (descending from left)
2. Sum products, compute mod 11
3. Check char: 0-9 or X (for 10)
```

### Mod 97 (ISO 7064) - Used by: IBAN, LEI
```
1. Move first 4 chars to end
2. Replace letters: A=10, B=11, ..., Z=35
3. Compute mod 97; valid if result = 1
```

### Weighted Sum - Used by: VIN, IMO, Routing Numbers
```
Position-specific weights multiplied by char values
```

---

## 🎯 Why These Matter for Compression

1. **Strict syntactic patterns** (fixed digit counts, check digits, separators) make them highly compressible when detected and normalized
2. **Check digit validation** allows filtering false positives before encoding
3. **Predictable check digits** can be stripped and recomputed on decode
4. **High frequency** in citations—millions of DOIs, ISBNs, PMIDs documented in Wikipedia citation datasets
5. **Special token patterns** or pre-processing dictionaries can reduce entropy in models/compressors

## 🛠️ Practical Extraction Strategies

### Regex Patterns
- ISBN/ISSN: fixed numeric/hyphen patterns with check digits
- DOI: prefix `10.` + registrant code + `/` + suffix
- arXiv: `arXiv:\d{4}\.\d{4,5}(v\d+)?`
- Coordinates: degree/minute/second patterns or decimal

### Template Parsing
- Wikipedia Cite templates: `|doi=`, `|isbn=`, `|pmid=`, `|issn=`, etc.
- Authority control sections: standardized with VIAF, ISNI, ORCID

### Validation Strategy
1. Extract candidate by regex
2. Validate check digit (if applicable)
3. High-confidence codes → special token encoding
4. Failed validation → treat as regular text
