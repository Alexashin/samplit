# Graph Report - samplit  (2026-10-28)

## Corpus Check
- 151 files · ~106,141 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 10 file(s) not represented in the graph (top: (none) 6, .woff 2, .ico 1)

## Summary
- 2101 nodes · 3652 edges · 125 communities (97 shown, 28 thin omitted)
- Extraction: 94% EXTRACTED · 6% INFERRED · 0% AMBIGUOUS · INFERRED: 209 edges (avg confidence: 0.85)
- Token cost: 0 input · 0 output

## Community Hubs (Navigation)
- Community 0
- Community 1
- Community 2
- Community 3
- Community 4
- Community 5
- Community 6
- Community 7
- Community 8
- Community 9
- Community 10
- Community 11
- Community 12
- Community 13
- Community 14
- Community 15
- Community 16
- Community 17
- Community 18
- Community 19
- Community 20
- Community 21
- Community 22
- Community 23
- Community 24
- Community 25
- Community 26
- Community 27
- Community 28
- Community 29
- Community 30
- Community 31
- Community 32
- Community 33
- Community 34
- Community 35
- Community 36
- Community 37
- Community 38
- Community 39
- Community 40
- Community 41
- Community 42
- Community 43
- Community 44
- Community 45
- Community 46
- Community 47
- Community 48
- Community 49
- Community 50
- Community 51
- Community 52
- Community 53
- Community 54
- Community 55
- Community 56
- Community 57
- Community 58
- Community 60
- Community 61
- Community 62
- Community 63
- Community 64
- Community 65
- Community 66
- Community 67
- Community 68
- Community 69
- Community 70
- Community 71
- Community 72
- Community 73
- Community 74
- Community 75
- Community 76
- Community 77
- Community 78
- Community 79
- Community 80
- Community 81
- Community 82
- Community 83
- Community 84
- Community 85
- Community 86
- Community 87
- Community 88
- Community 89
- Community 90
- Community 91
- Community 92
- Community 93
- Community 94
- Community 95
- Community 96
- Community 97
- Community 98
- Community 99
- Community 100
- Community 101
- Community 102
- Community 103
- Community 104
- Community 105
- Community 107
- Community 108
- Community 109
- Community 110
- Community 111
- Community 113
- Community 119

## God Nodes (most connected - your core abstractions)
1. `SamplitProcessor` - 162 edges
2. `KeyboardView` - 62 edges
3. `SampleBrowser` - 55 edges
4. `KitEngine` - 50 edges
5. `LicenseManager` - 44 edges
6. `GroupPanel` - 43 edges
7. `SamplitEditor` - 43 edges
8. `ZoneEditor` - 42 edges
9. `PresetPanel` - 40 edges
10. `Kit` - 39 edges

## Surprising Connections (you probably didn't know these)
- `started()` --references--> `Manifest`  [INFERRED]
  tests/license_manager_tests.cpp → core/include/samplit/core/Manifest.h
- `main()` --references--> `Manifest`  [INFERRED]
  tools/snapshot/main.cpp → core/include/samplit/core/Manifest.h
- `manager()` --references--> `Manifest`  [INFERRED]
  tools/snapshot/main.cpp → core/include/samplit/core/Manifest.h
- `build()` --references--> `AudioFormatManager`  [INFERRED]
  tools/packer/main.cpp → core/include/samplit/core/Pack.h
- `makePayload()` --calls--> `machineId()`  [INFERRED]
  tests/license_tests.cpp → core/src/License.cpp

## Import Cycles
- None detected.

## Communities (125 total, 28 thin omitted)

### Community 0 - "Community 0"
Cohesion: 0.03
Nodes (43): SamplitProcessor, activeHi, activeLo, cache, enabledOutputs, engine, failed, formats (+35 more)

### Community 1 - "Community 1"
Cohesion: 0.05
Nodes (23): SampleInfo, channels, duration, file, id, name, sampleRate, Spread (+15 more)

### Community 2 - "Community 2"
Cohesion: 0.05
Nodes (23): noteName(), altDown(), collectAudioFiles(), isAudioFile(), KeyboardView, dragFiles, groupBits, hintArea (+15 more)

### Community 3 - "Community 3"
Cohesion: 0.06
Nodes (23): hermite(), KitEngine, activeHi, activeLo, ageCounter, choicePick, current, cyclePos (+15 more)

### Community 4 - "Community 4"
Cohesion: 0.07
Nodes (34): buildPack(), BuiltPack, audio, file, key, manifest, "flac makes loud noise smaller than raw 24-bit but never loses a bit", "flac round trip is bit exact for 24-bit audio" (+26 more)

### Community 5 - "Community 5"
Cohesion: 0.06
Nodes (15): PickPanel, content, emptyText, hint, kit, PickPanel::PickPanel(), PickPanel::Row, index (+7 more)

### Community 6 - "Community 6"
Cohesion: 0.06
Nodes (28): "a folder becomes a kit: one file per key", "a group goes to its own output when the host enables it", "a new plugin has one empty group routed to Out 1", "a state with bad groups and zones is repaired", "buffer is cleared before rendering", "bus layouts: stereo Main required, extra outputs mono", constWav(), Fixture (+20 more)

### Community 7 - "Community 7"
Cohesion: 0.06
Nodes (17): SampleBrowser, allFiles, chooser, folderButton, folderLabel, lastPreviewedRow, lastPreviewTime, list (+9 more)

### Community 8 - "Community 8"
Cohesion: 0.11
Nodes (18): bez(), boxed(), f2(), icon_mac_svg(), icon_square_svg(), icon_svg(), lockup(), mark_paths() (+10 more)

### Community 9 - "Community 9"
Cohesion: 0.06
Nodes (33): "16 mono outputs: layout rules", "16 mono outputs: Main is stereo, Out 1..16 are mono and off by default", "kit: a key belongs to one piece", "kit: a layer with a single hit may repeat", "kit: a layer with pick cycle plays its hits in velocity order and wraps around", "kit: a layer with pick exact plays only the exact hit, without spread or repeats", "kit: a pick choice switches its layers between random and cycle", "kit: a piece does not choke itself" (+25 more)

### Community 10 - "Community 10"
Cohesion: 0.08
Nodes (21): "a licence file can be imported without a server", "a perpetual licence has no end date and ignores the clock", "activation without a server address reports an error", "an expired token gives no keys and a refresh picks up a new one", Env, clockNow, dir, pub (+13 more)

### Community 12 - "Community 12"
Cohesion: 0.10
Nodes (16): build_packs(), check(), dump(), expand_templates(), file_entry(), key_index(), load_json(), load_mappings() (+8 more)

### Community 13 - "Community 13"
Cohesion: 0.08
Nodes (26): "a token does not work on another machine", "a token signed by another key is rejected", "a valid token gives its pack keys", "canonical JSON sorts keys, drops spaces and escapes like Python", "changing a payload byte breaks the signature", "client refuses plain http and works without a network call on bad input", "client replaces the token only when the new one verifies", "expiry, grace and not-before with clock skew" (+18 more)

### Community 14 - "Community 14"
Cohesion: 0.15
Nodes (24): PackKey, key, packId, chunkAd(), generateKey(), get16(), get32(), get64() (+16 more)

### Community 15 - "Community 15"
Cohesion: 0.07
Nodes (12): PresetPanel, emptyText, folderButton, list, loadingName, presets, rescanButton, shownProgress (+4 more)

### Community 16 - "Community 16"
Cohesion: 0.12
Nodes (10): build_payload(), machine_id(), machine_key(), unwrap_pack_key(), verify_token(), wrap_pack_key(), code_hash(), test_machine_id_is_not_the_wrapping_key() (+2 more)

### Community 17 - "Community 17"
Cohesion: 0.07
Nodes (17): SamplitEditor, browser, groups, keyboard, lastFocused, license, logo, logoArea (+9 more)

### Community 18 - "Community 18"
Cohesion: 0.15
Nodes (5): hit(), MarkKitTest, rec(), synthetic_kit(), synthetic_mapping()

### Community 19 - "Community 19"
Cohesion: 0.07
Nodes (17): GroupPanel::Row, builtFor, colour, gain, gainLabel, index, mode, name (+9 more)

### Community 20 - "Community 20"
Cohesion: 0.08
Nodes (23): Kit, choices, id, kMaxChoices, mics, mode, notes, pieces (+15 more)

### Community 21 - "Community 21"
Cohesion: 0.08
Nodes (16): ZoneEditor, clearButton, emptyArea, group, headerArea, instrument, kButton, key (+8 more)

### Community 22 - "Community 22"
Cohesion: 0.20
Nodes (17): appendCanonical(), appendEscaped(), blake2b32hex(), canonicalJson(), fromBase64(), loadLastSeen(), machineFingerprint(), machineId() (+9 more)

### Community 23 - "Community 23"
Cohesion: 0.10
Nodes (19): "a failed or cancelled load keeps the current kit", fileOf(), loaderDir(), "loadKit applies pick rules: the layer follows the rule and becomes a switch", "loadKit builds a playable kit from a pack", "loadKit leaves unlicensed packs silent", "loadKit reports errors and cancel", "loadKit skips unverified hits unless asked" (+11 more)

### Community 24 - "Community 24"
Cohesion: 0.20
Nodes (17): _block64(), build_mapping(), Chunk, fastlz_decompress(), _file_list(), load(), note_name(), _parse_data_chunk() (+9 more)

### Community 25 - "Community 25"
Cohesion: 0.08
Nodes (12): GroupPanel, addButton, autoButton, content, footer, footerWarns, instrument, rows (+4 more)

### Community 26 - "Community 26"
Cohesion: 0.08
Nodes (15): LicensePanel, activateButton, busyShown, chooser, code, copyCodeButton, hintText, info (+7 more)

### Community 27 - "Community 27"
Cohesion: 0.11
Nodes (10): ActivateBody, create_app(), activate(), _bad_request(), renew(), revoke(), error(), RateLimiter (+2 more)

### Community 28 - "Community 28"
Cohesion: 0.16
Nodes (15): ClientConfig, allowInsecureHttp, serverUrl, timeoutMs, ClientResult, error, httpStatus, ok (+7 more)

### Community 29 - "Community 29"
Cohesion: 0.26
Nodes (14): Args, options, positional, asFile(), build(), info(), loadKey(), main() (+6 more)

### Community 30 - "Community 30"
Cohesion: 0.09
Nodes (22): "a group sends its sound to its own output", "a mono output receives the stereo voice as (L+R)/2", "a zone plays only on its key and velocity range", "a zone whose sample is not loaded is skipped", "active keys report sounding notes", "all mode layers every matching zone", "cycle mode goes through the variations in order", "each group picks on its own" (+14 more)

### Community 31 - "Community 31"
Cohesion: 0.11
Nodes (9): PackState, NoLicense, NotInstalled, Ready, PresetEntry, file, packs, preset (+1 more)

### Community 32 - "Community 32"
Cohesion: 0.09
Nodes (9): MixerPanel, content, emptyText, hint, kit, shownOutputs, strips, title (+1 more)

### Community 33 - "Community 33"
Cohesion: 0.15
Nodes (9): cpp_array(), fmt_time(), main(), parse_key_bytes(), Store, store(), test_admin_cli_flow(), test_pack_keys_are_encrypted_at_rest() (+1 more)

### Community 34 - "Community 34"
Cohesion: 0.18
Nodes (10): block_db(), category(), load_int16(), main(), stats(), mb(), pct(), trimmed_frames() (+2 more)

### Community 35 - "Community 35"
Cohesion: 0.10
Nodes (14): ChunkInfo, length, nonce, offset, PackWriter, chunks, data, dataFile (+6 more)

### Community 36 - "Community 36"
Cohesion: 0.10
Nodes (18): PickRule, includeExact, keyHi, keyLo, mode, name, pieces, Preset (+10 more)

### Community 37 - "Community 37"
Cohesion: 0.11
Nodes (11): LicenseManager, config, jobs, lastError, lastRenewTry, lifeToken, lock, onChange (+3 more)

### Community 38 - "Community 38"
Cohesion: 0.10
Nodes (12): MixerPanel::Strip, builtFor, index, level, mute, name, output, outputsBuilt (+4 more)

### Community 39 - "Community 39"
Cohesion: 0.10
Nodes (16): styleBar(), ZoneEditor::RowComponent, bright, file, gain, group, groupLabel, highVel (+8 more)

### Community 40 - "Community 40"
Cohesion: 0.11
Nodes (12): Header, chunkCount, fileSize, formatVersion, packId, PackReader, chunks, file (+4 more)

### Community 41 - "Community 41"
Cohesion: 0.15
Nodes (5): groupColour(), utf8(), levelText(), MixerPanel::MixerPanel(), panText()

### Community 42 - "Community 42"
Cohesion: 0.12
Nodes (9): MicState, levelDb, mute, out, pan, solo, main(), pumpUntil() (+1 more)

### Community 43 - "Community 43"
Cohesion: 0.22
Nodes (15): activate(), payload_of(), test_activate_errors(), test_activate_issues_a_verifiable_token(), test_activate_same_machine_again_is_fine_other_machine_is_not(), test_activation_attempts_are_rate_limited(), test_code_is_case_and_space_insensitive(), test_create_license_validates_input() (+7 more)

### Community 44 - "Community 44"
Cohesion: 0.18
Nodes (15): DecodedAudio, channels, frames, sampleRate, samples, decodeFlac(), encodeFlac(), fail() (+7 more)

### Community 45 - "Community 45"
Cohesion: 0.20
Nodes (9): extraOf(), fail(), intProp(), Manifest::findMic(), Manifest::fromJson(), Manifest::toJson(), num(), strProp() (+1 more)

### Community 46 - "Community 46"
Cohesion: 0.17
Nodes (6): Config, default_dir(), client(), config(), license_code(), test_init_does_not_overwrite_secrets()

### Community 47 - "Community 47"
Cohesion: 0.12
Nodes (13): SamplerEngine, activeHi, activeLo, ageCounter, current, cycle, fadeInStep, fadeLength (+5 more)

### Community 48 - "Community 48"
Cohesion: 0.11
Nodes (18): Layer, artic, choice, id, key, label, peerGroup, pick (+10 more)

### Community 50 - "Community 50"
Cohesion: 0.20
Nodes (9): applyPickRules(), ClipJob, clip, error, key, decodeJob(), isCancelled(), loadKit() (+1 more)

### Community 52 - "Community 52"
Cohesion: 0.19
Nodes (4): joinPacks(), packSummary(), PresetPanel::PresetPanel(), sizeText()

### Community 53 - "Community 53"
Cohesion: 0.14
Nodes (14): Layer, artic, extra, id, velHi, velLo, Piece, choke (+6 more)

### Community 54 - "Community 54"
Cohesion: 0.15
Nodes (3): gainText(), GroupPanel::GroupPanel(), panText()

### Community 56 - "Community 56"
Cohesion: 0.14
Nodes (14): kMaxMics, MicVoice, clip, fadeLeft, gain, mic, position, step (+6 more)

### Community 57 - "Community 57"
Cohesion: 0.14
Nodes (12): KitLoadResult, cancelled, error, kit, memoryBytes, missingPacks, mix, warnings (+4 more)

### Community 58 - "Community 58"
Cohesion: 0.14
Nodes (7): Rig, engine, inst, L, pl, pr, R

### Community 60 - "Community 60"
Cohesion: 0.15
Nodes (10): Manifest, extra, format, id, mics, pieces, requiresPacks, tier (+2 more)

### Community 61 - "Community 61"
Cohesion: 0.23
Nodes (4): AudioFormatManager, isAudioFile(), Library::open(), Library::scanFolder()

### Community 62 - "Community 62"
Cohesion: 0.15
Nodes (10): Zone, file, gainDb, group, highKey, highVel, lowKey, lowVel (+2 more)

### Community 63 - "Community 63"
Cohesion: 0.15
Nodes (12): Hit, files, id, key, layer, peerGroup, unverified, velHi (+4 more)

### Community 64 - "Community 64"
Cohesion: 0.18
Nodes (6): Rig, engine, L, pl, pr, R

### Community 65 - "Community 65"
Cohesion: 0.24
Nodes (3): render(), main(), main()

### Community 67 - "Community 67"
Cohesion: 0.24
Nodes (4): Library::count(), Library::search(), Statement, stmt

### Community 68 - "Community 68"
Cohesion: 0.17
Nodes (11): Group, gainDb, mode, name, output, pan, tracking, PlayMode (+3 more)

### Community 69 - "Community 69"
Cohesion: 0.17
Nodes (5): Sample, audio, file, sampleRate, makeSample()

### Community 70 - "Community 70"
Cohesion: 0.25
Nodes (7): cubic_split(), oklch_label(), pd(), ramp(), rule_step(), split_bg(), svg_inline()

### Community 71 - "Community 71"
Cohesion: 0.18
Nodes (7): "broken audio file is counted as failed and not indexed", "cancelled scan keeps rows that were not visited", "changed file is updated, deleted file is removed", "duration, sample rate and channels are read from the file", "first scan adds everything, second leaves it unchanged", "index survives reopening", "search matches every word, ignores case, looks at the path"

### Community 72 - "Community 72"
Cohesion: 0.18
Nodes (11): HitFile, chunk, end, extra, frames, gainDb, path, peak (+3 more)

### Community 73 - "Community 73"
Cohesion: 0.18
Nodes (10): LayerRef, choice, hits, layer, pick, velHi, velLo, NoteMap (+2 more)

### Community 74 - "Community 74"
Cohesion: 0.18
Nodes (11): Voice, age, fadeIn, fadeLeft, gainL, gainR, note, output (+3 more)

### Community 76 - "Community 76"
Cohesion: 0.20
Nodes (7): CheckResult, expires, licenseId, notBefore, packs, status, user

### Community 77 - "Community 77"
Cohesion: 0.20
Nodes (8): Options, clockSkewSeconds, clockStateFile, expiryGraceSeconds, fingerprint, now, publicKey, rollbackToleranceSeconds

### Community 78 - "Community 78"
Cohesion: 0.27
Nodes (4): DecodedClip, f32, s16, sampleRate

### Community 79 - "Community 79"
Cohesion: 0.20
Nodes (8): Config, allowInsecureHttp, dataDir, fingerprint, now, publicKey, renewIntervalHours, serverUrl

### Community 80 - "Community 80"
Cohesion: 0.20
Nodes (7): Info, busy, expires, lastError, licenseId, status, user

### Community 82 - "Community 82"
Cohesion: 0.31
Nodes (3): KitBuilder, kit, pumpUntil()

### Community 83 - "Community 83"
Cohesion: 0.25
Nodes (3): Library, db, openLibrary()

### Community 84 - "Community 84"
Cohesion: 0.28
Nodes (7): sqlite3, exec(), Known, id, mtime, size, Library::Library()

### Community 85 - "Community 85"
Cohesion: 0.22
Nodes (9): Status, BadSignature, ClockRollback, Expired, Malformed, NoToken, NotYetValid, Valid (+1 more)

### Community 86 - "Community 86"
Cohesion: 0.22
Nodes (5): VerifyReport, errors, warnings, describeRanges(), verifyManifest()

### Community 87 - "Community 87"
Cohesion: 0.22
Nodes (9): Hit, extra, files, id, key, layer, peerGroup, velHi (+1 more)

### Community 88 - "Community 88"
Cohesion: 0.22
Nodes (6): KitLoadOptions, cancel, keyFor, packDir, progress, threads

### Community 91 - "Community 91"
Cohesion: 0.31
Nodes (4): fillSamples(), TempDir, dir, samples

### Community 92 - "Community 92"
Cohesion: 0.32
Nodes (3): hexs(), ok(), status()

### Community 93 - "Community 93"
Cohesion: 0.25
Nodes (7): Mic, extra, id, name, out, role, operator""_s()

### Community 95 - "Community 95"
Cohesion: 0.25
Nodes (5): Pick, Cycle, Exact, Random, pickToString()

### Community 96 - "Community 96"
Cohesion: 0.25
Nodes (5): Retired, kit, preview, since, snapshot

### Community 97 - "Community 97"
Cohesion: 0.36
Nodes (3): KitFixture, kit, processor

### Community 98 - "Community 98"
Cohesion: 0.25
Nodes (3): canonical_json(), Signer, test_canonical_json_matches_cpp_contract()

### Community 99 - "Community 99"
Cohesion: 0.29
Nodes (3): Instrument, groups, zones

### Community 100 - "Community 100"
Cohesion: 0.29
Nodes (6): hex(), PayloadSpec, dropPacks, expires, fingerprint, notBefore

### Community 101 - "Community 101"
Cohesion: 0.33
Nodes (6): ScanResult, added, failed, removed, unchanged, updated

### Community 104 - "Community 104"
Cohesion: 0.33
Nodes (5): LoadedPack, id, key, manifest, reader

### Community 105 - "Community 105"
Cohesion: 0.33
Nodes (6): FileRef, gainDb, hit, job, mic, piece

### Community 107 - "Community 107"
Cohesion: 0.40
Nodes (3): scanner, folder, owner

### Community 108 - "Community 108"
Cohesion: 0.40
Nodes (3): HitSelect, Kontakt, Live

### Community 109 - "Community 109"
Cohesion: 0.40
Nodes (5): StrikeInfo, hit, layer, piece, voices

### Community 110 - "Community 110"
Cohesion: 0.40
Nodes (5): MicSetting, levelDb, mute, out, pan

## Knowledge Gaps
- **587 isolated node(s):** `id`, `file`, `name`, `duration`, `sampleRate` (+582 more)
  These have ≤1 connection - possible missing edges. (Counts symbols only; 1163 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **28 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `SamplitProcessor` connect `Community 0` to `Community 1`, `Community 2`, `Community 3`, `Community 5`, `Community 6`, `Community 7`, `Community 11`, `Community 15`, `Community 17`, `Community 20`, `Community 21`, `Community 25`, `Community 32`, `Community 37`, `Community 38`, `Community 41`, `Community 42`, `Community 45`, `Community 47`, `Community 52`, `Community 54`, `Community 55`, `Community 57`, `Community 61`, `Community 66`, `Community 81`, `Community 90`, `Community 95`, `Community 96`, `Community 97`, `Community 99`, `Community 106`, `Community 108`?**
  _High betweenness centrality (0.338) - this node is a cross-community bridge._
- **What connects `id`, `file`, `name` to the rest of the system?**
  _587 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Community 0` be split into smaller, more focused modules?**
  _Cohesion score 0.026973026973026972 - nodes in this community are weakly interconnected._
- **Why does `SamplitEditor` connect `Community 17` to `Community 32`, `Community 1`, `Community 2`, `Community 66`, `Community 0`, `Community 5`, `Community 7`, `Community 15`, `Community 49`, `Community 21`, `Community 25`, `Community 26`?**
  _High betweenness centrality (0.090) - this node is a cross-community bridge._
- **Should `Community 1` be split into smaller, more focused modules?**
  _Cohesion score 0.05403508771929825 - nodes in this community are weakly interconnected._
- **Why does `GroupPanel` connect `Community 25` to `Community 0`, `Community 1`, `Community 99`, `Community 103`, `Community 17`, `Community 19`, `Community 54`, `Community 94`?**
  _High betweenness centrality (0.072) - this node is a cross-community bridge._
- **Should `Community 2` be split into smaller, more focused modules?**
  _Cohesion score 0.05052631578947368 - nodes in this community are weakly interconnected._