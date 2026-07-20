# Retriever

Minimal and fast NTFS filename search via MFT/USN indexing

## Performance (NTFS, Windows 11, 928k entries across C:/D:/E:)

| operation                            | result         |
| ------------------------------------ | -------------- |
| index C: (864k entries), cold / warm | 12.6 s / 1.6 s |
| service RAM (928k entries)           | 116 MB         |
| substring `report` (1.1k hits)       | ~20 ms         |
| prefix / suffix / exact              | 2–5 ms         |
| suffix `.dll` (64k hits)             | ~5 ms          |
| path mode `system32` (30k hits)      | ~220 ms        |
| end-to-end client call               | ~70 ms         |

Live updates (create/rename/delete) land in the index in under 1.5 s via
the USN journal monitor.

## Build

CMake and a C11 compiler:

```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

## Usage

```
retriever                        # index all fixed NTFS/ReFS volumes (run elevated)
retriever C: D:                  # specific volumes

rtv report.docx                    # search (default: case-insensitive substring)
rtv -m suffix -n 50 .pdf           # modes: name prefix suffix exact path regex
rtv -m regex 'img_\d.jpg'          # whole-name shape: \d number, \a letters, \p specials
rtv                                # interactive prompt
rtv stats | ping | version
```
