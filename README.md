# DSA-Assignment

## Smart Duplicate Text-File & Content Detector

A local C++ command-line utility that scans registered text files, groups exact
duplicates, and optionally reports likely near-duplicates. It reads files as
bytes, so exact duplicate checks are not affected by text encoding or line
endings. Unreadable paths are reported and skipped.

### Build and run

Requires a C++17 compiler.

```powershell
g++ -std=c++17 -Wall -Wextra -pedantic src\main.cpp -o duplicate_detector.exe
.\duplicate_detector.exe
```

Use the menu to register files, scan them, and toggle near-duplicate mode.
Paths containing spaces can be entered normally. Each scan processes the
registered paths in FIFO order.

### Detection rules

- **Exact duplicates:** FNV-1a 64-bit fingerprints select candidate files.
  The full contents are compared before files are grouped, so a hash collision
  never counts as proof of equality.
- **Near-duplicates (optional):** Text is lowercased and runs of whitespace
  are collapsed. The scanner compares sets of normalized words using Jaccard
  similarity and reports pairs at or above 80%. This is a candidate indicator,
  not a claim that two files are interchangeable.
- **Storage savings:** For each exact group, keeping one copy saves the sizes of
  the remaining copies. Near-duplicate candidates are not counted as savings.
- Readable files are listed in ascending size, then name order.

### Data structures and algorithms

| Structure / algorithm | Role |
| --- | --- |
| Strings and byte arrays | Hold file contents and normalized text |
| Custom linked queue | Process registered paths in FIFO order |
| Linked list | Keep scanned file records |
| Hash table (`std::map` buckets keyed by fingerprint) | Find candidate exact-match groups |
| Full string comparison | Verify fingerprints and guard against collisions |
| Sorting | Order the report by size and path |
| Token sets and Jaccard similarity | Find optional near-duplicate candidates |

### Suggested three-member task division

1. **File ingestion:** Register paths, read content, normalize it, and report
   missing or unreadable files.
2. **Fingerprint engine:** Implement fingerprints, candidate grouping, and
   exact-content collision verification.
3. **Reports and batch processing:** Implement the linked queue, reports,
   sorting, interactive menu, and tests.

### Live demonstration

1. Create three text files: two with identical contents and one different.
2. Register all three and scan. The identical pair should appear in one exact
   duplicate group.
3. Edit one file, scan again, and observe the group update.
4. Optionally toggle near-duplicate mode and try two files with mostly matching
   words but a small difference.

### Viva talking points

- **Can two files have the same hash?** Yes. Fingerprints are candidate keys;
  this program compares full contents before declaring an exact duplicate.
- **Why normalize text?** Near-duplicate matching can ignore case and
  whitespace differences. The exact normalization rules are stated above.
- **What does a fingerprint do?** It is a compact key that narrows down which
  files need full-content comparison.
