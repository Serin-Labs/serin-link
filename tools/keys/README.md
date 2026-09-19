# Serin Link production verification key

`serin-link-release.pub` is the RSA-3072 **public** key trusted by production
release staging. It contains no private material.

Its SHA-256 fingerprint over DER SubjectPublicKeyInfo is:

```text
935cd0d3f264367edee4933c85172a41ff1f0be63a889bcdb9a0b7aee960f14d
```

This key matches the first signature block in both published 0.1.6 factory
apps at offset `0x20000`; both app signatures verify with `espsecure` 4.12.0.
The public artifacts are in
[`Serin-Labs/serin-cn105/firmware/link`](https://github.com/Serin-Labs/serin-cn105/tree/main/firmware/link):

| Factory image | Full image SHA-256 |
| --- | --- |
| `serin_dial_viewe15_factory.bin` | `7e7de57b04973908a811ae5e5e2f3ef0e1df83a3c27d5f16e0f76df5edd61a3d` |
| `serin_dial_viewe21_factory.bin` | `e799817a067f20e9be175b9bc356c3aa0db212e71d3e5e783657db705d9a9ff8` |

The private key is not in this repository. Tests use disposable keys.

Changing this file changes which images the publisher accepts. It does not
change the trust held by installed devices, which accept only updates signed
with this key.
