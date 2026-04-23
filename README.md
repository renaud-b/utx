# Utopixia CLI (`utx`)
Version: 1.0 (long-form documentation, draft)  
Last update: 2026-02-24

`utx` is the Git-like command-line tool for **Utopixia**: a distributed, multi-chain, **graph-native** infrastructure where code and data are stored as verifiable structures and can be reconstructed deterministically.

This document is meant to be readable as a “real manual”, while staying faithful to the current reference implementation (C++23). If you are maintaining `utx`, you can treat this as a living spec.

---

## Table of contents

1. What `utx` is (and what it isn’t)
2. The mental model: code → graph → actions/snapshots → chain
3. Multi-chain by default
4. Repo layout and configuration files
5. Installing / running / debug mode
6. Project initialization
7. Identity and authentication
8. Tracking files (`utx add`)
9. Ignore rules (`.utxignore`, `utx ignore`)
10. Status (`utx status`)
11. Commit (`utx commit`) — structural diff, strategies, revision files
12. Push (`utx push`) — segmentation, genesis, parallelism, partial pushes
13. Uncommit (`utx uncommit`)
14. API tools (`utx api`)
15. Graph tools (`utx graph`)
16. Chain tools (`utx chain`)
17. Download from network (`utx download`)
18. Practical workflows (examples)
19. Troubleshooting and “why did this happen?”
20. Security and determinism notes
21. Glossary

---

## 1. What `utx` is (and what it isn’t)

### What it is

`utx` is a deployment protocol in CLI form. It prepares and emits blockchain payloads to Utopixia’s network, so that peers can reconstruct a **graph state** deterministically and serve or rebuild artifacts (web pages, code, identities…).

The key thing is that `utx` versions **structure**, not text.

### What it isn’t

`utx` is not a Git replacement in the “one global repo history” sense, because Utopixia does not rely on a single chain. You can still use Git locally if you want, but Utopixia’s unit of deployment is a **graph chain**, not a monolithic repository history.

---

## 2. The mental model: code → graph → actions/snapshots → chain

In Utopixia, every deployable artifact is represented as a **structured graph**. A file becomes:

```
source file
  → language parser
  → structured graph (AST / DOM / CSS tree / generic graph)
  → structural diff vs remote graph
  → actions (incremental) OR snapshot (full state)
  → on-chain payloads
```

On the receiving side, any node can rebuild:

```
(latest snapshot) + (actions after snapshot) → deterministic graph state
```

If the artifact is a language with a generator (HTML/JS/C++…), the graph can be reconstructed back into source code. The goal is to make this reconstruction stable and verifiable.

A direct implication: **formatting noise disappears**. A purely textual “reindent everything” change may result in a tiny or even empty structural diff, depending on the parser.

---

## 3. Multi-chain by default

Utopixia is multi-chain by design. `utx` embraces this by making **one chain per target file** the default.

That means:

- no global lock or “one repo ordering” bottleneck,
- natural sharding (each file evolves independently),
- parallel pushes (many chains can be updated concurrently),
- independent ownership and labels per artifact.

This applies to:

- HTML pages and web assets (HTML/JS/CSS),
- C++ sources and headers,
- generic structured graphs,
- user identities (a user is a chain).

---

## 4. Repo layout and configuration files

A typical repository:

```
project/
├─ .utx.deploy.json         # Versioned deployment manifest (tracked targets)
├─ .utxignore               # Ignore rules (gitignore-like)
├─ .utx/
│  ├─ config.json           # Local project config (wallet, api target, deploy chain)
│  └─ revisions/            # Pending local commits (revision files)
├─ src/
├─ web/
└─ ...
```

### `.utx.deploy.json` (versioned)

This is the canonical “what is tracked and where it goes” manifest. You generally want to commit this file to your normal VCS if you use one.

Each entry (“target”) contains:

- `path`: path relative to repo root
- `chain`: chain id (UUID-like string)
- `kind`: the parser / projector kind (html/js/css/cpp/graph/identity)
- `last_revision_id`: non-empty when a commit is staged for push
- `last_synced_hash`: local hash used by `status`
- `genesis_labels`: optional user-defined labels to set at chain creation time

### `.utx/config.json` (local, not versioned)

Local session configuration:

- `wallet_path`: filesystem path to a wallet JSON
- `api_target`: host:port for the Singularity API endpoint
- `deploy_chain`: a dedicated chain id used to snapshot the deploy manifest itself on-chain

This file is local by nature and should not be committed.

### Revision files: `.utx/revisions/rev_*.utx`

A revision file is the output of `utx commit`. It is a **list of lines**, where each line is already a chain payload that `utx push` will broadcast.

A revision file typically contains, for each modified target:

- either a snapshot line: `urn:pi:graph:snap:<base64_json>`
- or several action lines: `urn:pi:graph:action:<encoded_action>`
- and finally one commit-tag line: `urn:pi:graph:action:T...` (a COMMIT_TAG action)

The commit-tag line is what allows `utx push` to **route the preceding segment to the correct chain**.

---

## 5. Installing / running / debug mode

`utx` is a native CLI written in C++23.

At runtime it expects:

- access to the repository files (`.utx.deploy.json`, `.utx/config.json`, tracked sources),
- an API endpoint reachable at `api_target` (unless you only do local operations like `status`),
- a wallet configured (for any command that emits blocks).

### Debug mode

You can run:

```bash
utx --debug <command> ...
```

This increases log verbosity and is useful when diagnosing commit/push behavior.

---

## 6. Project initialization

### `utx init`

```bash
utx init
```

Creates:

- `.utx.deploy.json` (empty manifest)
- `.utx/config.json` (local config, including an auto-generated deploy chain id)

If the project is already initialized (deploy file exists), it does nothing.

---

## 7. Identity and authentication

In Utopixia, **a user is a chain**, and your wallet identifies you (address + keys).

### `utx identity create`

```bash
utx identity create <wallet_path> <pseudo> [--target <api>]
```

This command:

- generates a keypair (if needed),
- materializes the identity chain if it does not exist,
- emits an action setting `user.pseudo`,
- stores the wallet file locally,
- writes `.utx/config.json` with wallet path and API target.

Example:

```bash
utx identity create ~/.utx/wallet.json <username>
```

### `utx identity show`

```bash
utx identity show
```

Fetches the identity graph and prints:

- address (chain id),
- pseudo (from `user.pseudo`).

### `utx identity b64`

```bash
utx identity b64
```

Outputs a base64-encoded config blob (handy for environment variables or scripts).

### `utx login`

```bash
utx login <wallet_path> [--target <api>]
```

Sets the active wallet and API target in `.utx/config.json`. If the identity graph exists, it prints a “welcome back” message including the pseudo.

### `utx logout`

```bash
utx logout
```

Clears the local wallet reference. (It does not delete your wallet file.)

---

## 8. Tracking files (`utx add`)

### What `add` means in Utopixia

`utx add` does not “stage content” like Git. Instead, it registers a file (or a directory of files) into the deployment manifest with:

- a stable chain id,
- a kind (parser/projector),
- optional genesis labels.

After that, `utx status` and `utx commit` can include that target.

### `utx add`

```bash
utx add <path> [--chain <id>] [--kind <kind>] [--force] [--label <label>] [--labels a,b,c]
```

Examples:

```bash
utx add web/index.html --label website --label landing
utx add src/main.cpp
utx add web/           # recursive add
```

#### How chain ids are chosen

For a single file:

- if you pass `--chain <id>`, that chain id is used,
- else if the file is already tracked and has a chain id, it is reused,
- otherwise a new UUID is generated.

For directories, `--chain` is forbidden because each file must have its own chain.

#### How kinds are chosen

If you do not pass `--kind`, `utx` deduces from the file extension:

- `.html` / `.htm` → html
- `.js` / `.mjs` / `.cjs` → js
- `.css` → css
- `.cpp` / `.hpp` → cpp
- everything else → graph

You can force the kind explicitly when needed.

#### Genesis labels

Genesis labels are stored in `.utx.deploy.json` and applied **only when the chain is created** (genesis block).

You can pass labels with `--label` repeated, or `--labels a,b,c`.

Note: Identity chains are special. The reference `push` logic avoids applying custom labels for identity chains.

---

## 9. Ignore rules

Utopixia’s CLI has two layers of ignore behavior:

1) “Hard ignore” internal files
2) User-configurable `.utxignore`

### Hard ignore (non-negotiable)

`utx` always ignores:

- `.utx/` and everything under it
- `.utx.deploy.json`
- `.utxignore` (as deployable content)

This is a safety rule: these files are local/project metadata, not content to deploy as a target artifact.

### `.utxignore`

`.utxignore` behaves like `.gitignore`:

- `*` matches any sequence except `/`
- `?` matches one character except `/`
- `**` matches across directories
- a trailing `/` means directory-only
- `!` negates a pattern

The CLI also comes with conservative default ignores (examples: `.git/`, `node_modules/`, `build/`, `cmake-build-*`).

### `utx ignore init`

```bash
utx ignore init
```

Creates a starter `.utxignore` file if missing.

### `utx ignore add`

```bash
utx ignore add <pattern>
```

Appends a rule to `.utxignore`.

---

## 10. Status inspection (`utx status`)

### `utx status`

```bash
utx status
```

This is designed to be fast. It uses file hashes (MD5 in the current implementation) and the values stored in `.utx.deploy.json`.

It reports each tracked target in one of these states:

- `CLEAN`: local file hash equals `last_synced_hash`
- `MODIFIED`: local file hash differs; needs commit
- `COMMITTED`: `last_revision_id` is set; ready to push
- `DELETED`: tracked path missing on disk
- `UNTRACKED`: files in the repo not present in the manifest (excluding ignored)

Important: `status` does not parse AST graphs and does not fetch remote chain state. It is a local signal, not a remote verification step.

---

## 11. Commit workflow (`utx commit`)

### `utx commit`

```bash
utx commit "message" [--force-snapshot] [--force-group-actions] [--group-action-size N] [--push]
```

A commit builds a local revision file. Nothing is pushed to the network unless you add `--push`.

#### Options

`--force-snapshot`  
Forces snapshot strategy for modified targets.

`--force-group-actions`  
Forces incremental strategy (grouped actions) for modified targets.

`--group-action-size N`  
Controls how many primitive actions are packed into each GROUP action.

`--push`  
Automatically runs `utx push` after the revision is created.

The two force flags are mutually exclusive.

---

### What actually happens during commit (per target)

For each tracked target file:

1) **Hash check**  
   If local hash equals `last_synced_hash`, the file is skipped.

2) **Parse local file into graph**  
   Parsing depends on `kind`:
- html: HTML parser produces actions that rebuild a DOM graph
- js: JS parser produces AST root element
- css: CSS parser produces structured graph
- cpp: C++ parser produces actions that rebuild an AST/token graph
- graph: generic path (depends on your project’s graph format)

3) **Fetch remote graph state**  
   `utx` queries `/graph/<chain_id>` from the API.

If the chain has no graph yet (not found / unreachable), the commit behaves as “first deploy”.

4) **Compute structural diff**  
   If remote exists: `diff = GraphDiffer::compute_diff(remote_root, local_root)`  
   If remote does not exist: `diff` is produced by emitting actions that materialize the entire local graph under the root.

5) **Batch and serialize incremental candidate**  
   Actions are grouped into GROUP actions of up to `group_action_size`. Then serialized into lines:
   `urn:pi:graph:action:<encoded_group_action>`

6) **Build snapshot candidate**  
   A snapshot is built by serializing the whole local graph to JSON and base64-encoding it:
   `urn:pi:graph:snap:<base64_json>`

7) **Choose strategy by size (unless forced)**  
   Default rule: snapshot is used when its payload size is smaller than the incremental payload size.

8) **Append a COMMIT_TAG action**  
   A commit tag is always appended to end the segment for this target. It contains metadata including:
- the target chain id
- the file path
- the selected strategy
- per-target revision id (hash of this segment)
- author address
- timestamp
- number of blocks that will be emitted for this segment

9) **Write revision file**  
   All per-target segments are concatenated into one revision file:
   `.utx/revisions/rev_<global_rev_id_prefix>.utx`

The global revision id is computed as a hash of the full revision content.

10) **Update manifest**  
    Targets that participated get `last_revision_id = <global_rev_id>`.  
    The updated `.utx.deploy.json` is written.

---

## 12. Push workflow (`utx push`)

### `utx push`

```bash
utx push
```

`utx push` reads the current revision file and broadcasts it to the network.

### How a revision is segmented

A revision file is a stream of payload lines. `utx push` groups these lines into **segments**.

A segment ends when a COMMIT_TAG action line is encountered (`urn:pi:graph:action:T...`).

`utx push` decodes the COMMIT_TAG payload to extract:

- `target_chain` (which chain to send the segment to)
- `file_path` (for logs and local manifest updates)

This design has a nice property: the revision file itself is “self-routing”.

### Chain creation (genesis) if missing

For each chain, before sending segment blocks, `utx push` checks if the chain exists by fetching its last block. If it does not exist, it creates a genesis block.

Genesis includes:
- owners (the wallet address),
- a projector name chosen from the target kind (Graph/Js/Web/Cpp/Css/Identity),
- labels.

Default labels include:

- `project:<repo_name>`
- `kind:<kind>`
- `path:<file_path>`

Custom genesis labels from `.utx.deploy.json` are merged in (except identity targets).

### Emitting blocks

For each payload line in the segment, `utx push` emits a block with:

- increasing index / nonce,
- previous hash chaining,
- timestamp,
- payload data line as-is (the same `urn:...` string),
- signature computed from the wallet.

### Parallelism

Push is parallelized across chains. Each chain segment can be pushed concurrently.

Because each file is its own chain, this gives real throughput benefits.

### Partial pushes

If one chain fails to push (network error, node rejects block, etc.), other chains can still succeed.

At the end:
- `.utx.deploy.json` is saved with updated hashes for successful chains.
- The revision may remain partially pending until you re-run `utx push`.

### Post-success cleanup

If **all chains** in the revision succeeded:

1) The deploy manifest itself is snapshotted and pushed to the “deploy chain” (`deploy_chain` in `.utx/config.json`).
2) Local cleanup runs:
    - clears `last_revision_id` for all targets referencing this revision,
    - deletes the revision file.

---

## 13. Uncommit (`utx uncommit`)

### `utx uncommit`

```bash
utx uncommit
```

Deletes the pending revision file and clears `last_revision_id` in `.utx.deploy.json`.

This is a local rollback of staging, not a network operation.

---

## 14. API management (`utx api`)

### `utx api set`

```bash
utx api set <host:port>
```

Sets the API target in `.utx/config.json`.

### `utx api check`

```bash
utx api check
```

Queries the current API endpoint for reachable peers and prints cluster status.

---

## 15. Graph tools (`utx graph`)

These are low-level utilities useful for debugging.

### `utx graph root`

```bash
utx graph root <chain_id>
```

Fetches the graph state and prints root info.

### `utx graph show`

```bash
utx graph show <chain_id> [--depth <n>]
```

Prints nodes recursively (the reference implementation prints full recursion; the `--depth` flag is currently informational and may be expanded).

### `utx graph update`

```bash
utx graph update <chain_id> <element_id> <property> <value>
```

Emits a SET action to update one property on-chain.

---

## 16. Chain tools (`utx chain`)

These are power tools for manual chain manipulation.

### `utx chain create`

```bash
utx chain create [--labels "a,b,c"] [--kind <kind>] [--chain_id <id>] [--projector <name>]
```

Creates a genesis block for a new chain.

### `utx chain emit`

```bash
utx chain emit --chain_id <id> --content "<payload>"
```

Emits a raw payload block on a chain. This is intentionally low-level and should be used carefully.

---

## 17. Download from network (`utx download`)

This command bootstraps a local workspace from the deploy manifest chain.

```bash
utx download <manifest_chain_id> [--api-target <host:port>] [--dry-run]
```

What it does:

- fetches the graph stored on the manifest chain,
- reconstructs `.utx.deploy.json`,
- fetches each target chain referenced by the manifest,
- restores each local file from the graph state,
- saves `.utx/config.json` with the manifest chain id.

With `--dry-run`, `utx download` still fetches and decodes the manifest plus each target chain, but it does not write any local files or config.

Supported reconstruction strategies:

- HTML: `HtmlGenerator`
- JavaScript: `JsGenerator`
- CSS: `CssGenerator`
- C++: `CppGenerator`
- JSON: graph-to-JSON reconstruction
- Graph / identity: raw graph JSON fallback

If the local project is not initialized yet, `utx download` still works because it bootstraps the workspace directly from the network.

---

## 18. Practical workflows

### A. Create a project and deploy a web page

```bash
mkdir mysite && cd mysite
utx init

utx login ~/.utx/wallet.json --target 127.0.0.1:8080

mkdir -p web
echo '<!doctype html><html><body>Hello</body></html>' > web/index.html

utx add web/index.html --label website
utx status

utx commit "first deploy"
utx push
```

After push, your `web/index.html` chain exists, and nodes can rebuild its DOM graph deterministically.

### B. Iterate quickly (commit locally, push later)

```bash
utx commit "work in progress"
# do other edits...
utx commit "more work"
# now push when ready
utx push
```

(If the tool blocks a second commit because a revision is pending, use `utx push` or `utx uncommit` first.)

### C. Force snapshot for big structural changes

```bash
utx commit "major refactor" --force-snapshot
utx push
```

---

## 19. Troubleshooting and “why did this happen?”

### “Why does `status` say CLEAN but the network is different?”

`status` is based on local `last_synced_hash`. It does not fetch remote state. If a push was partial or you changed API targets, local state may not reflect reality.

Use `utx push` again, or fetch graph state via `utx graph show`.

### “Why did `commit` choose SNAPSHOT?”

Because the snapshot payload size (base64 JSON) was smaller than the incremental payload size, unless you forced a strategy.

This can happen when:
- many nodes changed,
- reorderings produce lots of MOVE/DELETE/SET operations,
- your action encoding overhead is large.

### “Why is my diff huge after a small change?”

Typical causes:
- parser differences (version mismatch, options, or stability issues),
- node id generation not stable (causes “everything looks new”),
- the local parse graph is structurally different from what remote expects,
- whitespace/token emission options differ (especially for C++ strict round-trip mode).

### “Why does push create a genesis even though I thought the chain existed?”

If `utx push` cannot fetch the last block (`/chain/<addr>/last` returns 404 or network error), it may assume the chain is missing. Confirm API target and connectivity.

### “Push is partial. What should I do?”

Run `utx push` again. The revision file remains until full success cleanup. Successful chains should have updated `last_synced_hash` already.

---

## 20. Security and determinism notes

- `utx` relies on deterministic parsers and deterministic action application. If parsers are not stable, structural diffs can become noisy.
- The tool signs every emitted block using the active wallet. Keep wallet files safe.
- Multi-chain parallel pushes improve throughput, but also increase surface area for partial failures. The CLI is designed to handle partial push recovery.

A key philosophical point: the system guarantees deterministic replay, not canonical minimal diffs. Different action sequences may produce the same final projected state; history can be meaningful.

---

## 21. Glossary

**Target**: an entry in `.utx.deploy.json` representing one deployable artifact.  
**Kind**: a parser/projector family (html/js/css/cpp/graph/identity).  
**Chain**: a blockchain address hosting an artifact’s evolution.  
**Graph**: structured representation of code/data; rebuilt from snapshot + actions.  
**Action**: transformation applied to a graph state (SET/DELETE/MOVE/GROUP/COMMIT_TAG).  
**Snapshot**: base64-encoded JSON full graph state for fast rebuild.  
**Revision file**: local file containing chain payload lines to push.  
**Commit tag**: an action that terminates a chain segment and provides routing metadata.

---
End of document.
