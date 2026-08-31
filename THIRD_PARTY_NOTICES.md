# Third-party notices

## Mozilla CA certificate store

`data/cacert.bin` is the Mozilla CA certificate store converted to PEM and
distributed by the curl project at <https://curl.se/docs/caextract.html>.

- Bundle date: 2026-08-13
- SHA-256: `f66dff1bdf8f96060b8177976f8b7d9254bc89bc4db933d769f7384d28480bc9`
- License: Mozilla Public License 2.0

The bundle is included so libcurl can validate certificate chains on Nintendo
Switch without disabling peer or hostname verification.
