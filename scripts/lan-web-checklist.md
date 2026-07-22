# Bridged LAN web-demo checklist

Interactive verification (not run in CI). Requires macOS QEMU with `vmnet-bridged`.

## Host prep

```bash
networksetup -listallhardwareports   # note Wi-Fi/Ethernet device, often en0
make iso
PEAK_NET_MODE=bridged PEAK_NET_IFACE=en0 ./scripts/run-qemu.sh
```

macOS may prompt for network permissions / require an elevated QEMU.

## In Peak

```text
ifconfig
# expect a LAN inet address (dhcp) or documented fallback

cd /home/dev/workspace/web-demo
ctr build -t peak/web:latest
ctr run -p 8080 --name peak-web peak/web:latest
ctr ps
# STATUS should include Up/listen
```

## From the Mac host

```bash
curl -v http://<guest-ip>:8080/
curl -v http://<guest-ip>:8080/style.css
```

Expect HTML title “Peak Web Demo” and `text/css` for the stylesheet.

## From a second LAN computer

```bash
curl http://<guest-ip>:8080/
```

## Teardown

```text
ctr stop peak-web
ctr ps
```

Port 8080 should no longer accept connections.

## Negative checks

- [ ] `gui` idle does not print TLS certificate warnings on serial
- [ ] User-net smoke (`make smoke-bios`) still passes without bridged mode
