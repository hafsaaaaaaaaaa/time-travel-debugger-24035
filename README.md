# Time-Travel Debugger (TTDB)

Server side of a time-travel debugger. The client sends a program written in C-- (`source.bin`), and the server runs it, records the program state after every line, and writes a `.tdbg` file the client can step through forwards and backwards.

This repo covers Phase 01 (server). Phase 02 (client) comes later.

## C-- in a nutshell

Instructions: `func`, `func_end`, `call`, `set`, `add`, `sub`, `mul`, `div`

Each line is `Keyword Identifier Params/Args`, with at most 16 params. For `add`, `sub`, `mul` and `div` the result goes into the first parameter, and changes made inside a called function show up in the caller.

```
func foo b
set a 10
add b a
func_end
func main
set k 10
call foo k
func_end
```

## Pipeline

Work through these in order. Each stage gets its own commit.

- [ ] **Stage 0: Receive** - read `source.bin` from the client
- [ ] **Pass 0x0: Validate** - check every `func` has a matching `func_end`, reject nested declarations, send the error back on failure
- [ ] **Pass 0x1: Resolve** - write `resolve.bin` as `[offset 8B][size 4B][string]` per line, then patch each `call` with the byte offset of its target function. Missing `main` or a call to an undefined function is an error
- [ ] **Pass 0x2: Execute** - tokenize and run from `main`, keep a call stack of frames, record a snapshot into the timeline after every line
- [ ] **Pass 0x3: Serialize** - write `session.tdbg` with header, snapshot stream and dense index

## .tdbg layout

1. Header: magic `TTDB`, version, stepCount, indexOffset
2. Snapshot stream: one snapshot per executed line
3. Dense index: offset of each snapshot, so step N can be found directly

## Build and run

```
make
./ttdb sample/demo.src
```

## Layout

```
include/   headers for each pass and shared types
src/       implementation
sample/    test programs
```

## Status

Phase 01 deadline: 5 October 2026.
