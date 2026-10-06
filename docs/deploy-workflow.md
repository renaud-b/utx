# UTX deploy workflow

## Invariant

A deploy is a one-shot operation.

`utx` sends the current content of each modified tracked target to a node, signs the deployment plan returned by that node, submits it immediately, and marks the target synchronized only after the deployment reaches `Finalized`.

There is no persistent local "committed but not pushed" deployment state.

## Why

A deployment plan is computed against current remote graph state. Keeping such a plan locally and submitting it much later creates a stale-state boundary that does not exist in Git commits.

The node is also the authoritative implementation of content parsing, graph diffing, and snapshot/action strategy selection. Keeping copies of those algorithms in `utx` makes the client dependent on planner implementation details and risks divergence from node behavior.

## Consequence

The normal project workflow is:

```text
utx add <path>
edit files
utx status
utx deploy "message" [--force-snapshot]
```

Modified target chains are deployed independently and concurrently.

A target updates its local `last_synced_hash` only after its deployment reaches `Finalized`. Other chains may still succeed if one chain fails.

After all modified targets succeed, `.utx.deploy.json` is synchronized to the project's deploy chain through the same deploy protocol using node-side JSON planning. This preserves `utx download` without requiring a JSON-to-graph parser in the CLI.

## Limits

A multi-chain deploy is not a global atomic transaction. Partial success is possible by design.

`utx deploy` therefore reports failure when any target fails, while retaining the successful targets as synchronized locally.

Offline preparation is intentionally not part of the current workflow. If a future use case requires review or delayed submission, it should be introduced as a separate explicit operation rather than changing the semantics of `deploy`.


## Reset after a network reinitialization

## Invariant

`utx reset` invalidates local synchronization proof and removes legacy genesis metadata that the current deploy protocol cannot replay.

It clears every tracked target's `last_synced_hash` and `genesis_labels`. Chain IDs, file paths, kinds, wallet configuration, API target, deploy-chain ID, and local files are preserved.

## Why

`last_synced_hash` records that a local content hash reached `Finalized` on the network known at that time. After a network reinitialization, that historical fact no longer proves the target exists on the current network.

`genesis_labels` is legacy manifest metadata. The current V1 prepare/submit protocol does not carry it, so preserving those labels across a reset would make the next deploy fail before reseeding can begin.

## Consequence

```text
utx reset
utx status
utx deploy "redeploy after network reset"
```

Existing tracked files appear modified again and are eligible for deployment.

The operation is idempotent: running `utx reset` repeatedly does not alter project topology.

## Limits

Resetting synchronization state does not add deploy-protocol capabilities. Legacy genesis labels are deliberately discarded because they cannot be represented by the current V1 deploy protocol.
