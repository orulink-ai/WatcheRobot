# App.Center sample catalog

This directory is a minimal App.Center catalog for local testing.

For the architecture boundary and manifest contract, see
`../docs/app_center_app_pack_contract.md`.

It currently contains one downloadable app-pack sample:

- `Pixel Game`

`pixel_game.example.pkg` is a minimal manifest package used by App.Center
download, install, open, uninstall, and list-status tests.

The `.pkg` extension is a sample-catalog app-pack extension. The desktop
App.Center workbench can also import it for package inspection and transfer
testing.

Serve this directory from your computer, then set `CONFIG_APP_CENTER_REMOTE_LIST_URL`
to the served `apps.json` URL, for example:

```text
http://<computer-lan-ip>:8767/apps.json
```

From the workspace root, you can write that URL into the ESP32 sdkconfig with:

```powershell
yarn appcenter:url http://<computer-lan-ip>:8767/apps.json
```

Or let the workspace choose the first private LAN IP and write the URL:

```powershell
yarn appcenter:prepare
```

Preview without changing sdkconfig:

```powershell
yarn appcenter:prepare:dryrun
```

Validate the sample catalog before serving it:

```powershell
yarn appcenter:validate
```

The `packageUrl` in `apps.json` can be relative. App.Center resolves it against
the `apps.json` URL, so `pixel_game.example.pkg` is downloaded from the same
directory.

## User-visible lifecycle

From the device App.Center UI, each catalog entry has a simple phone-app style
lifecycle:

- Not installed: selecting the app downloads the package from `packageUrl`,
  validates it, stores it in the device App.Center package store, and marks it
  installed.
- Installed: selecting the app can open the installed package runtime or shell.
- Uninstall: removes the device-side installed package and metadata, then the
  app returns to the downloadable state.

Uninstalling an App.Center package does not delete the package file from this
computer-side sample catalog, does not modify `apps.json`, and does not affect
Launcher-local apps such as BLE App, Client App, Voice App, Provision App, or
App.Center itself.

## App package manifest

The first app-pack format is a small JSON manifest. It is enough for App.Center
to download, install, uninstall, and open a downloaded app shell.

Supported fields:

- `name` / `title` / `appName`: display name
- `description` / `desc` / `message`: text shown when opening the app shell
- `state` / `stateId` / `behavior`: behavior state id
- `anim` / `animation` / `animId`: animation id
- `permissions`: optional string array. Current sample-safe values are
  `servo`, `display`, `sound`, and `storage`
- `signature`: optional object. Local samples may use
  `algorithm: "unsigned-dev"`. Production packages should use
  `algorithm: "ecdsa-p256-sha256"` with `digest`, `issuer`, `publicKeyPem`, and
  base64 DER `signature` / `value`.

Unsigned local samples may use `algorithm: "unsigned-dev"` while developing.
The device applies the App.Center trust policy:
`CONFIG_APP_CENTER_ALLOW_UNSIGNED_DEV_PACKAGES` controls unsigned-dev packages,
`CONFIG_APP_CENTER_TRUSTED_SIGNATURE_ISSUERS` can restrict production issuers,
and `CONFIG_APP_CENTER_TRUSTED_SIGNATURE_PUBLIC_KEY_SHA256` is the production
public-key trust anchor for ECDSA P-256 signed packages.

Example:

```json
{
  "manifestVersion": 1,
  "id": "pixel-game",
  "name": "Pixel Game",
  "version": "game-v1",
  "description": "A tiny downloaded app-pack demo",
  "state": "standby",
  "anim": "standby",
  "permissions": ["display"],
  "signature": {
    "algorithm": "unsigned-dev",
    "issuer": "local-sample",
    "digest": "unsigned-local-sample"
  }
}
```

To make a new downloadable app appear in App.Center, add an entry to `apps.json`:

```json
{
  "id": "pixel-game",
  "name": "Pixel Game",
  "description": "A tiny downloaded app-pack demo",
  "packageUrl": "pixel_game.pkg",
  "version": "game-v1"
}
```

Do not add local Launcher functions such as BLE App, Client App, Voice App,
Provision App, or App.Center itself to this catalog. App.Center filters local
app ids/names and only acts as the downloadable app store.

Example local server:

```powershell
cd firmware\s3\app_center_sample
python -m http.server 8767
```

Any `app_center_sample_server*.log` files produced during local testing are
machine-local diagnostics and should not be committed.

The workspace helper validates the catalog before starting the HTTP server:

```powershell
yarn appcenter:sample
```

The validator checks that relative package files exist, declare
`manifestVersion`, match the catalog app id/version when those fields are
present, and include signature metadata such as `unsigned-dev` for local
samples. This keeps the sample catalog, README snippets, and package manifests
from drifting apart.

Use the printed URL whose IP address is on the same LAN as the device, usually
`192.168.x.x`, `10.x.x.x`, or `172.16.x.x` to `172.31.x.x`. VPN or virtual
adapter addresses usually cannot be reached by the device.

To validate the catalog and print the candidate `apps.json` URLs without
starting the blocking HTTP server:

```powershell
yarn appcenter:sample:dryrun
```

To smoke-test that the sample catalog is also reachable over HTTP locally, and
that every catalog `packageUrl` can be fetched and parsed as an app-pack
manifest:

```powershell
yarn appcenter:smoke
```

Run all local App.Center catalog checks in one command:

```powershell
yarn appcenter:check
```
