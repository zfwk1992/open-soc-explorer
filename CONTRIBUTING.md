# Contributing to Open SoC Explorer

Thank you for helping. Open SoC Explorer is a fork of
[Ripes](https://github.com/mortbopet/Ripes); please read
[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/DEVELOPMENT_GUIDE.md](docs/DEVELOPMENT_GUIDE.md) first.

## Ground rules

1. **No fabricated hardware behaviour.** Anything the GUI shows, explains or
   scores must come from simulator output, a recorded trace or the ISA
   specification. Label derived data with its observability level
   ([docs/TRACE_PROTOCOL.md](docs/TRACE_PROTOCOL.md)).
2. **State fidelity.** Do not describe educational models as real hardware,
   and do not mix time domains of different engines
   ([docs/SIMULATION_FIDELITY.md](docs/SIMULATION_FIDELITY.md)).
3. **Tests for every feature.** Add or extend QtTest suites in `test/`
   (and `tools/trace` tests for Python code). Quote only numbers that a test
   measures.
4. **Keep upstream diffs small.** Prefer new files under `src/explorer` or new
   tabs over changing Ripes code; when you must change Ripes code, keep the
   change minimal and mention it in the commit message.
5. **Licensing.** New code is MIT with `// SPDX-License-Identifier: MIT`.
   Never remove existing notices. Third-party code/assets need a compatible
   license and an entry in [docs/LICENSING.md](docs/LICENSING.md).

## Workflow

1. Create a branch from `main` (`feature/...`, `fix/...`, `sync/ripes-...`).
2. Build and run all tests (see the development guide).
3. Format changed C++ files with `clang-format-15 -i`.
4. Small, focused commits with descriptive messages.
5. Open a pull request; CI must be green (`Ripes Tests` on all platforms,
   `Check code format`, `Open SoC Explorer checks`).
6. Never force-push shared branches; merge upstream Ripes instead of
   rebasing onto it.

## Good first contributions

* A new mission (format in [docs/LEARNING_GUIDE.md](docs/LEARNING_GUIDE.md)),
  with a test proving it is solvable.
* Descriptions for more datapath components (`src/explorer/componentinfo.cpp`),
  e.g. those of the multi-cycle and dual-issue models.
* Translations of mission texts.
* Trace converters for other engines (gem5, Ibex) following the protocol.

## Reporting issues

Include the processor model, the program, the cycle where the behaviour is
visible and, if possible, an exported trace (`.jsonl`).
