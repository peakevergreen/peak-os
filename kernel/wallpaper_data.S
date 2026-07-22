/* Embedded default desktop wallpaper (binary PPM P6). */
.section .rodata
.align 8
.globl wallpaper_evergreen_ppm
.globl wallpaper_evergreen_ppm_end
.globl wallpaper_evergreen_ppm_len

wallpaper_evergreen_ppm:
    .incbin "assets/wallpapers/evergreen.ppm"
wallpaper_evergreen_ppm_end:

.align 8
wallpaper_evergreen_ppm_len:
    .quad wallpaper_evergreen_ppm_end - wallpaper_evergreen_ppm
