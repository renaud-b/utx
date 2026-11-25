# Safe project download

Invariant: a remote manifest cannot choose paths outside the destination or
replace existing files. Validate every path before writing; use descriptor-relative
exclusive creation without following symlinks. Internal CLI and Git paths are
reserved. Download into a fresh directory; existing target/manifest/config files
are refused rather than overwritten. Existing non-symlink directories are allowed.

Artifact downloads use the node's published descriptor and raw /render bytes.
The CLI checks size, SHA-256 and the manifest's existing MD5 synchronization hash.
This is consistency with node-reported state, not independent consensus verification.
Graph targets still use the existing local generators and current graph state.

Dry runs do not write. If a target fails, other already-restored files may remain;
no project metadata is created until all targets succeed. There is no transactional
whole-directory install; retry in a clean directory. The local destination is
assumed not to be concurrently renamed by a hostile local process. Linux/POSIX
exclusive open protects against links and overwrites, not arbitrary local attackers.
No downloaded code is executed.

## Publisher preview

Download obtains the manifest's public owner observation, reads the manifest
at that head hash, rechecks the observation, and presents every owner address
and available profile. A changed manifest aborts before writes. This is
node-reported ownership, not independent finality or a safety review. Referenced
file chains are still read at their current state, not pinned by the manifest.
Default [y/N]; EOF or any other answer cancels without writes. --yes explicitly
approves; --dry-run does not write or prompt. Old nodes without /owners fail closed.
No wallet or code execution. Remote profile strings are sanitized for terminal output.
