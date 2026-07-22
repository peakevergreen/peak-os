# Peak Container Runtime (in-guest)

Peak builds and runs Dockerfiles **inside the guest** using the VFS, then serves
them over a real TCP HTTP listener (and to Peak Browser via `ctr_http_get`).

```
Guest ctr build  →  /var/lib/peak-ctr/images/<tag>/rootfs/
Guest ctr run    →  TCP listen on chosen port + virtual Browser GET
LAN / Browser    →  HTTP GET → rootfs files
```

This is **not** OCI/runc. `FROM` does not pull images; `COPY` stages files into a
rootfs; `run` opens a Peak TCP listener and serves static files. There is no host
Docker daemon and no COM bridge.

## Quick demos

```bash
./scripts/run-qemu.sh
```

### Local Browser demo (port 18080)

```text
cd /home/dev/workspace/demo
ctr build
ctr run
gui
```

Peak menu → **Browser** → `http://127.0.0.1:18080/` → Enter.

### LAN webpage demo (port 8080)

```text
cd /home/dev/workspace/web-demo
ctr build -t peak/web:latest
ctr run -p 8080 --name peak-web peak/web:latest
ifconfig
```

From another computer on the same LAN (bridged QEMU — see [network.md](network.md)):

```text
curl http://<guest-ip>:8080/
curl http://<guest-ip>:8080/style.css
```

## Guest commands

| Command | Meaning |
|---------|---------|
| `ctrd` | Init runtime + ping |
| `ctr ping` | Health check |
| `ctr build [path] [-t tag]` | Parse Dockerfile, COPY into image store |
| `ctr run [-p port] [--name n] [img]` | Listen + serve image (default `18080`) |
| `ctr ps` / `logs` / `stop` | Inspect / tear down |

## Dockerfile subset

Supported:

- `COPY <src> <dest>` — copy a single file from build context into image rootfs

Quarantined (logged, never pulls):

- `FROM <base>` — **does not fetch** a base image from any registry. Peak ctr is
  COPY-only. For demo Dockerfiles that mention `nginx`, ctr may mkdir
  `/usr/share/nginx/html` locally so COPY targets exist — that is path scaffolding,
  not an image pull. Build logs emit `QUARANTINE FROM …` and a final warning when
  a Dockerfile used FROM.

Ignored (logged): `WORKDIR`, `CMD`, `EXPOSE`, `ENV`, `RUN`, `ENTRYPOINT`.

## Limits

Not a real containerd. No BuildKit, no image pulls, no network namespaces, no
process isolation. HTTP is a small static-file server (GET/HEAD) bound to the
container port. Expose only on trusted LANs.

See [docs/from-scratch.md](from-scratch.md) and [docs/network.md](network.md).
