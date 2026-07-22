# Sound drivers (placeholder)

Portable sound leaf drivers will live here.

- x86 PC speaker / sound: `kernel/arch/x86_64/sound.c`
- Raspberry Pi audio: `kernel/platform/rpi/sound.c` (stub until PWM/I2S/HDMI beep)

Do not add board-specific code here until a portable sound HAL seam is ready.
