# ADR 0020: Persistent identifiers

- **Status:** Accepted
- **Date:** 2026-11-14
- **Related:** AGENTS.md §3 (stable project-owned IDs, no raw pointers, GPU IDs or EnTT handles as identity), ADR 0003 (deterministic generation), ADR 0004 (IDs as strings in files), `docs/milestones/m4-scene-data-model.md` M4.1

## Context

M4 gives entities, scenes and (from M4.3) assets an identity that is saved in documents and restored when they are loaded. The identity must not depend on memory addresses, GPU handles, EnTT's internal entity values or the order of creation in one session, and entities must keep their IDs when they move between documents (merged scenes, prefab instances, copy and paste in a later editor).

## Options

| Option | Generation | Collision risk | Text form | Verdict |
|---|---|---|---|---|
| Counter per scene | next value | none in one scene; collisions as soon as entities move between documents | short | rejected: depends on the document it was made in |
| Random 64-bit | seeded generator | about n²/2^65: 2.7·10⁻⁸ for 10⁶ entities | 16 hex digits | adequate for one scene, weak for a whole project |
| Random 128-bit in the UUID version 4 layout (122 random bits) | seeded generator | about n²/2^123: below 10⁻²⁵ for 10⁶ entities | 36 characters, recognized by tools | **chosen** |
| Hash of a name or path | derived | changes with a rename | — | rejected: not an identity |

## Decision

1. **Value:** `engine::core::Uuid`, 128 bits as two `std::uint64_t` (`high`, `low`), compared and ordered as one big-endian number. All zero is the **null** ID, never a valid identity.
2. **Layout:** generated IDs follow RFC 9562 version 4: the version nibble (`high` bits 12–15) is 4 and the variant (the top two bits of `low`) is binary 10; the other 122 bits are random.
3. **Generation:** `engine::core::UuidGenerator`, a value type owned by its user, with xoshiro256** (Blackman and Vigna) as the source: a 256-bit state, two outputs per ID. `UuidGenerator::fromEntropy()` fills the state from `std::random_device` (identity does not need to be reproducible); `UuidGenerator(seed)` expands a 64-bit seed with SplitMix64, for tests with golden values that match on every platform. No global or thread-local generator; nothing comes from a world seed, from the `RandomStream`s of ADR 0003 or from `std::hash`, so creating an ID never consumes world-generation randomness.
4. **Text form:** the canonical 36-character form `xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx`, written in lower case. Reading is strict: exactly 36 characters, hyphens at positions 8, 13, 18 and 23, hex digits elsewhere (upper case accepted); any version and variant are accepted, so IDs made by other tools can be read. Messages quote at most 40 characters of the input, which may come from untrusted files. In JSON the ID is a string (ADR 0004 rule 2).
5. **Typed IDs:** `core::TypedUuid<Tag>` wraps a `Uuid` per kind of object; `scene::EntityId` and `scene::SceneId` now, `assets::AssetId` in M4.3. Different kinds are different C++ types without conversions between them, are not constructible from integers or pointers, and name their kind in parse errors (`entity ID '…': …`). Parsing a typed ID rejects the null ID.
6. **Uniqueness:** a scene rejects an entity ID it already contains with an error that names the scene and the ID, and rejects the null ID. A generated ID that is null or already present is drawn again, at most 16 times, then the scene reports an error (with 122 random bits this never happens in practice). The ID of a destroyed entity can only come back through explicit creation with that ID (loading, a later undo), never by generation.

## Consequences

- IDs are 16 bytes in memory and 36 characters in files; maps keyed by them are ordered maps or hashed with a project hasher, never `std::hash` as identity.
- Tests: golden IDs of the seeded generator from an independent implementation, version and variant bits, uniqueness over a large sample, text round trip, every parse error, typed-ID separation checked at compile time.
- `std::random_device` is used only by `UuidGenerator::fromEntropy()`; generation code stays deterministic.
