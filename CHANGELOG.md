# Changelog

All notable changes to StruckRoom. Versions follow the git tags; the format is
[Keep a Changelog](https://keepachangelog.com/).

## [Unreleased]

### Changed

- Help opens the help page in a window of the plugin's own, not in the web browser. Nothing is
  written to the temp folder, and on Linux no web server runs in the host any more.

## [0.9.0] - 2026-11-14

First public preview.

### Added

- StruckSynth's struck object in a room whose every length is measured in wavelengths of the note,
  so the whole room transposes with it.
- Wall effects (Shift, Flex, Rattle, Gap) and wall materials; with tracking off, a plain physical
  room.
- MIDI 2.0 notes taken directly, so a Pitch 7.9 note plays its exact pitch; MPE and CLAP note
  expressions too.
- CLAP plugin for Linux (x86-64) and macOS 26 or later (Apple Silicon).

### Known gaps

- See the README's Known gaps section.
