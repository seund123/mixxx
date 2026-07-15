# AGENTS.md — Mixxx Project Instructions

See [README.md](README.md) for a project overview, and
[CONTRIBUTING.md](CONTRIBUTING.md) for build instructions, code style,
pre-commit setup, Git workflow, and pull request guidelines.

## Key Architecture

- **ControlObject/ControlProxy**: `[Group], key_name` inter-component communication.
- **Engine thread**: Real-time audio — no allocations, no locks, may emit Qt signals but cannot receive them.
- **parented_ptr/make_parented**: Qt object-tree ownership. Object must get a parent before `parented_ptr` destructs.

## AI DJ (smart Auto DJ)

- New logic lives in `src/library/aidj/` (non-RT). Ranking implements `ITrackRanker`.
- Wired through `AutoDJFeature` (`EnableSmartQueue` pref). Do not put ranking in `src/engine/`.
- Beatmatched transitions: `TransitionPlanner` + `AutoDJProcessor::prepareToDeckForTransition`
  (`AutoSyncOnTransition` / `AutoKeylockOnTransition` prefs).
- ML embeddings should add another `ITrackRanker` implementation; keep AutoDJ queue + transitions.

## Project Layout

```text
src/ C++ source (engine/, controllers/, library/, mixer/, effects/, qml/, preferences/, util/, test/)
res/ Resources (controllers/ JS/XML, skins/, qml/)
cmake/ CMake modules
tools/ Python helper scripts
```
