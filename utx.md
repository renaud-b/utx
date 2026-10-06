# Utopixia CLI (`utx`)
Version: 1.1 (long-form documentation, draft)  
Last update: 2026-10-06

`utx` is the deployment command-line tool for **Utopixia**: a distributed, multi-chain, **graph-native** infrastructure where code and data are stored as verifiable structures and can be reconstructed deterministically.

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
11. Deploy (`utx deploy`) — one-shot prepare / sign / submit
12. API tools (`utx api`)
13. Graph tools (`utx graph`)
14. Chain tools (`utx chain`)
15. Download from network (`utx download`)
16. Practical workflows (examples)
17. Troubleshooting and “why did this happen?”
18. Security and determinism notes
19. Glossary

---

## 1. What `utx` is (and what it isn’t)

### What it is

`utx` is a thin deployment client. It tracks local targets and asks Utopixia nodes to prepare deployment plans, so that peers can reconstruct a **graph state** deterministically and serve or rebuild artifacts (web pages, code, identities…).

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
│  └─ config.json           # Local project config (wallet, api target, deploy chain)
├─ src/
├─ web/
└─ ...
```

### `.utx.deploy.json` (versioned)

This is the canonical “what is tracked and where it goes” manifest. You generally want to commit this file to your normal VCS if you use one.

Each entry (“target”) contains:

- `path`: path relative to repo root
- `chain`: chain id (UUID-like string)
- `kind`: the parser / projector kind (html/js/css/markdown/cpp/graph/identity)
- `last_synced_hash`: local hash used by `status`
- `genesis_labels`: legacy manifest metadata; the current V1 deploy protocol does not carry custom genesis labels

### `.utx/config.json` (local, not versioned)

Local session configuration:

- `wallet_path`: filesystem path to a wallet JSON
- `api_target`: host:port for the Singularity API endpoint
- `deploy_chain`: a dedicated chain id used to snapshot the deploy manifest itself on-chain

This file is local by nature and should not be committed.


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

This increases log verbosity and is useful when diagnosing deploy behavior.

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
utx identity create <wallet_path> <pseudo> [--target <api>] [--with-projector <name>]
```

This command:

- generates a keypair (if needed),
- materializes the identity chain if it does not exist,
- emits an action setting `user.pseudo`,
- stores the wallet file locally,
- writes `.utx/config.json` with wallet path and API target.

Additional projectors can be attached to the identity chain at genesis with
`--with-projector`. The option is repeatable. `OwnerProjector` and
`IdentityProjector` are managed automatically.

Example:

```bash
utx identity create ~/.utx/wallet.json <username> \
  --with-projector DecentralizedProjector@AaEOWoA5fw-CvpqICMJSMQ
```

This creates the identity chain with:

```text
OwnerProjector
DecentralizedProjector@AaEOWoA5fw-CvpqICMJSMQ
IdentityProjector
```

The projector composition is immutable after genesis.

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
- a parser/projector kind supported by the current node deploy planner.

After that, `utx status` and `utx deploy` can include that target.

### `utx add`

```bash
utx add <path> [--chain <id>] [--kind <kind>] [--force]
```

Examples:

```bash
utx add web/index.html
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
- `.md` / `.markdown` → markdown
- `.cpp` / `.hpp` → cpp
- everything else → graph

You can force the kind explicitly when needed.

Go parsing/generation still exists in the CLI, but **Go deployment is temporarily rejected** because the current V1 node deploy planner does not route `kind=go` through its Go parser/projector path.

#### Genesis labels

The manifest still understands the legacy `genesis_labels` field for backward compatibility, but the current V1 `prepare/submit` protocol has no labels field. New `utx add --label/--labels` requests are therefore rejected instead of silently losing the labels.

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
- `MODIFIED`: local file hash differs; needs deploy
- `DELETED`: tracked path missing on disk
- `UNTRACKED`: files in the repo not present in the manifest (excluding ignored)

Important: `status` does not parse AST graphs and does not fetch remote chain state. It is a local signal, not a remote verification step.

---

## 11. Deploy workflow (`utx deploy`)

### `utx deploy`

```bash
utx deploy "message" [--force-snapshot]
```

A deploy is a **one-shot network operation**. There is no local committed-but-not-pushed state.

For every modified tracked target, `utx`:

1. reads the current file and compares its local hash with `last_synced_hash`;
2. sends the raw content, target kind, chain id and message to the node deploy protocol;
3. receives the node-produced deployment plan;
4. signs the planned transactions with the active wallet;
5. submits them immediately;
6. waits for the submitted blocks to reach `Finalized`;
7. updates `last_synced_hash` only for targets that finalized successfully.

Modified chains are deployed concurrently and independently. A failure on one chain does not roll back successful chains.

The node — not the CLI — owns parsing, graph diffing and snapshot/action strategy selection. `--force-snapshot` is a policy request forwarded to that planner.

After all modified targets succeed, `utx` publishes the updated `.utx.deploy.json` to the project's `deploy_chain` through the same protocol using `kind=json`. This keeps `utx download` reconstructible without embedding a JSON-to-graph compiler in the CLI.

A multi-chain deploy is not globally atomic. If one target fails, successful targets remain synchronized and the failed target remains `MODIFIED` for the next deploy.

---
## 12. API management (`utx api`)

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

## 13. Graph tools (`utx graph`)

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

## 14. Chain tools (`utx chain`)

Chain creation remains a high-level deploy concern. Opaque writes to an existing
chain are available through the V1 prepare/submit admission path.

### `utx chain create`

Direct genesis submission was removed from the public node API. A chain is now created implicitly by the first supported deployment:

```bash
utx add web/index.html
utx deploy "first deploy"
```

### `utx chain emit`

```bash
utx chain emit --chain_id <id> --content "<payload>"
```

Sends one opaque transaction to an existing chain using the currently logged-in
wallet.

The command uses the V1 deploy admission protocol:

```text
prepare(kind=raw)
  -> sign with current wallet
  -> submit
  -> pending/finalization
```

It does **not** call the removed legacy `/chain/{id}/transaction` endpoint.

Example:

```bash
utx chain emit \
  --chain_id <identity-chain> \
  --content "urn:pi:capability:init:family-v1"
```

Raw mode cannot create a chain; the target chain must already exist.


## 15. Download from network (`utx download`)

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
- Markdown: `MarkdownGenerator`
- C++: `CppGenerator`
- JSON: graph-to-JSON reconstruction
- Graph / identity: raw graph JSON fallback

If the local project is not initialized yet, `utx download` still works because it bootstraps the workspace directly from the network.

---

## 16. Practical workflows

### A. Create a project and deploy a web page

```bash
mkdir mysite && cd mysite
utx init

utx login ~/.utx/wallet.json --target 127.0.0.1:8080

# Or import a base64 identity config from a file.
# Keeping the secret out of argv avoids leaking it through shell history/process listings.
# The embedded api_target is reused unless --target overrides it.
utx login --b64-file ./identity.b64

mkdir -p web
echo '<!doctype html><html><body>Hello</body></html>' > web/index.html

utx add web/index.html
utx status

utx deploy "first deploy"
```

After push, your `web/index.html` chain exists, and nodes can rebuild its DOM graph deterministically.

### B. Iterate quickly

Edit files freely, inspect local changes, then deploy the current state in one shot:

```bash
utx status
utx deploy "update"
```

There is no local commit queue. If delayed/offline preparation is needed in the future, it will be exposed as a separate explicit operation rather than changing `deploy` semantics.
### C. Force snapshot for big structural changes

```bash
utx deploy "major refactor" --force-snapshot
```

---

## 17. Troubleshooting and “why did this happen?”

### “Why does `status` say CLEAN but the network is different?”

`status` is based on local `last_synced_hash`. It does not fetch remote state. If you changed API targets or external tools modified a chain, local state may not reflect that remote history.

Use `utx graph show <chain_id>` to inspect remote graph state. A later `utx deploy` always asks the node to plan against its current canonical state.

### “Why did deploy choose SNAPSHOT?”

The strategy is selected by the node-side deploy planner from the current remote graph, the submitted content and policy flags. Use `--force-snapshot` when an explicit full-state deployment is required.

### “Why is my diff unexpectedly large?”

Parsing and graph diffing are node responsibilities. Typical causes include parser/version differences, unstable node identifiers, structural reorderings, or a local source whose parsed structure differs significantly from the canonical remote graph.

### “Deploy is partial. What should I do?”

Run `utx deploy` again after fixing the reported error. Targets that already reached `Finalized` have their `last_synced_hash` updated locally; failed targets remain `MODIFIED` and are replanned against current network state.

---
## 18. Security and determinism notes

- Nodes are responsible for deterministic parsing, graph diffing and action/snapshot planning.
- `utx` signs the transactions produced by the node plan using the active wallet. Keep wallet files safe.
- Multi-chain parallel deploys improve throughput but allow partial success by design; failed targets are safely replanned on retry.

A key philosophical point: the system guarantees deterministic replay, not canonical minimal diffs. Different action sequences may produce the same final projected state; history can be meaningful.

---

## 19. Glossary

**Target**: an entry in `.utx.deploy.json` representing one deployable artifact.  
**Kind**: a parser/projector family (html/js/css/markdown/cpp/graph/identity).  
**Chain**: a blockchain address hosting an artifact’s evolution.  
**Graph**: structured representation of code/data; rebuilt from snapshot + actions.  
**Action**: transformation applied to a graph state (SET/DELETE/MOVE/GROUP/COMMIT_TAG).  
**Snapshot**: base64-encoded JSON full graph state for fast rebuild.  
**Commit tag**: an action that terminates a chain segment and provides routing metadata.

---
End of document.
