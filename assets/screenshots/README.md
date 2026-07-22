# Screenshots

Expected files (referenced from the root README):

| File | Capture tip |
|------|-------------|
| `desktop.png` | Boot to GUI desktop wallpaper + windows |
| `shell.png` | Terminal / shell prompt visible |
| `browser.png` | Browser window open |
| `sysmon.png` | System monitor (Peak → Monitor or `sysmon`) |

## Regenerate with QEMU

```bash
export PATH="/opt/homebrew/opt/llvm/bin:$PATH"
make iso
# Graphical QEMU with a monitor socket:
qemu-system-x86_64 -machine q35 -m 512 -cdrom build/peak-os.iso \
  -serial stdio -display cocoa \
  -monitor unix:/tmp/peak-qemu.mon,server,nowait
# In another terminal, after the desktop is up:
echo "screendump assets/screenshots/desktop.png" | socat - UNIX-CONNECT:/tmp/peak-qemu.mon
```

Keep images reasonably sized (≈1280px wide is enough for GitHub).
