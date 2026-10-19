# Public controller protocol compatibility

Run from the repository root:

```sh
test/sl2_public_compat/run.sh
```

`test/run-all.sh` discovers this group automatically. It requires GCC, standard
shell tools, `sha256sum`, and a little-endian host; it needs no ESP-IDF, hardware,
or network. Temporary binaries and outputs use a unique
`mktemp` directory, removed on exit.

The same C probe is compiled separately against `main/sl2_proto.h` and the
unmodified public controller fixture. Its outputs must agree on:

- All 22 shared packed structures: total size, and every member's offset and
  width, including arrays, reserved bytes, and nested catalog entries.
- All 164 shared numeric constants: packet and TLV types, enums, feature and
  command bits, version/length gates, sentinel values, namespaces, and limits.
- Public PMK and LMK context bytes; representative STATE, CMD, CAPS, sensor,
  catalog/source, credential relay, Wi-Fi session/cancel, and pairing packets;
  pairing transcript and LMK salt helpers; INFO TLV encoding/decoding; short
  sensor frame decoding. STATE, CMD, and sensor vectors also assert independently
  written expected little-endian bytes, including signed temperatures, centi
  versus deci units, sentinel values, and replay epochs.

`wire_contract.def` is an explicit checklist, not a header parser. Text identity
is not required. Controller-only helpers/constants (for example the external
source namespace and certificate TLV) and dial-only helpers/header views do not
make their shared messages incompatible. Update the checklist if the common
wire surface grows. This regression does not test the controller state machine,
real cryptographic operations, radio behavior, or hardware interoperability.
Current pairing requires v5 on both peers even though tolerant frame decoding
has an earlier compatibility floor.

## Pinned upstream and license

Public repository: <https://github.com/Serin-Labs/serin-link-core>

Immutable revision: `86cfdd202441c980129d776a8c8d830014548a6b`.
This is the selected public controller snapshot, not an alias for the `v0.1.5`
tag (`dbfe1b5586ad5a0a9b2de8dbc2dacda4a0e56378`).

Unmodified upstream files:

- [include/serin_link/sl2_proto.h](https://github.com/Serin-Labs/serin-link-core/blob/86cfdd202441c980129d776a8c8d830014548a6b/include/serin_link/sl2_proto.h)
  → `upstream/sl2_proto.h`
- [LICENSE](https://github.com/Serin-Labs/serin-link-core/blob/86cfdd202441c980129d776a8c8d830014548a6b/LICENSE)
  → `upstream/LICENSE`

The fixture retains **Apache-2.0**, including the full upstream license and all
header notices. Upstream has no root NOTICE file at this revision. The harness
and checklist are project-authored GPL-3.0-only. `upstream/SHA256SUMS` records the
SHA-256 of both imported files and is checked before compilation; it pins the
fixture's integrity, not byte identity with the dial header.

## Updating the pin deliberately

1. Choose and review an immutable public upstream commit. Inspect the protocol
   changes and its LICENSE/NOTICE files. Do not replace the fixture merely to
   silence a failure caused by an accidental dial change.
2. Download the raw header and full license from that commit; retain source
   notices unchanged and include any applicable upstream NOTICE file. For
   example, substitute the reviewed 40-character revision for `<commit>`:

   ```sh
   curl --fail --location 'https://raw.githubusercontent.com/Serin-Labs/serin-link-core/<commit>/include/serin_link/sl2_proto.h' -o test/sl2_public_compat/upstream/sl2_proto.h
   curl --fail --location 'https://raw.githubusercontent.com/Serin-Labs/serin-link-core/<commit>/LICENSE' -o test/sl2_public_compat/upstream/LICENSE
   (cd test/sl2_public_compat/upstream && sha256sum sl2_proto.h LICENSE > SHA256SUMS)
   ```

3. Update the revision and immutable links here, plus the third-party inventory
   if notices or paths change. Review the fixture diff and hashes together.
4. Extend `wire_contract.def` and vectors for any new shared fields, messages,
   or constants. Preserve historical version/length gates and confirm any
   intentionally different extensions are capability/version gated.
5. Run this group and `test/sl2_proto/run.sh`. Confirm detection with a temporary
   dial-header edit, such as swapping two equally sized fields, changing an
   enum value, or changing a transcript domain byte: each must make this group
   fail. Restore the edit and rerun successfully before committing the update.
