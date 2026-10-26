# Utopixia CLI (`utx`)

`utx` is the command-line deployment tool for Utopixia. It lets you initialize
a project, track and stage files, deploy selected content, and download files
from the network.

## Install

For Linux amd64:

```bash
curl https://raw.githubusercontent.com/renaud-b/utx/refs/heads/main/install.sh | bash
```

## Quick Start

```bash
utx init
utx login <wallet_path>
utx add <file>
utx status
utx deploy "message"
```

`add` selects files for the next deployment; only staged paths are deployed.
After finalization the selection is cleared. You can stage a directory recursively.
The deployment planner runs on the node; there is no local commit/push queue.

## Documentation

See [utx.md](./utx.md) for the full manual and
[docs/deploy-workflow.md](./docs/deploy-workflow.md) for deployment invariants.
