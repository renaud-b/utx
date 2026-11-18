# Publishing opaque Artifact files with utx

## Invariant
An Artifact target is published byte-for-byte. The CLI never parses its AST.
The node prepares chunks; utx validates every planned chunk against the
local file before signing. Only finalized deployments become synchronized.

## Usage
~~~bash
utx add dist/assets/app.js --kind artifact
utx add dist/assets/logo.png        # artifact inferred from .png
utx add dist --kind artifact        # all files in dist are opaque
utx deploy "Publish compiled website"
~~~

Source extensions (.js, .css, .html, .json) retain structural parsing by
default, so use --kind artifact for compiled versions. Common binary extensions
(images, fonts, WASM, PDF) are automatically classified as Artifact.
Each file keeps its own chain in .utx.deploy.json. The MIME type is derived
from its extension, falling back to application/octet-stream.

## HTTP boundary
- Artifact prepare sends kind=artifact, content_b64, and mime.
- The prepared genesis uses the existing textual payload_data field.
- Binary chunk plan entries use payload_data_b64.
- The CLI canonical-decodes and checks version SHA-256, metadata, indices,
  and chunk bytes against the original file before wallet authorization.
- The signed transaction uses raw bytes in the unchanged signing preimage.
- Submit sends data_b64 for binary chunks, legacy data for genesis/source.
- The CLI waits for pending receipts to reach Finalized before marking clean.

Base64 is only a JSON transport envelope, never on-chain file content.

## Limits
- The current node prepare limit is 8 MiB per file, chunks of 24 KiB.
- Multi-chunk admission is asynchronous: queued does not imply finalized.
  Real network finalization tests are still required.
- Failed/partial publications stay staged for explicit retry; the node
  authoritative pending state selects the resume index.
- utx download does not yet reconstruct opaque Artifact chains. Adding the
  verified /render byte path to download should be a separate follow-up PR.
- Public routing and SPA fallback are separate capabilities.
