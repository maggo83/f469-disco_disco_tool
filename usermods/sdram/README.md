# SDRAM as block device

For temporary storage: the RAM disk region of `sdram_layout.h`, which assigns all SDRAM regions.

Usage:

```py
import sdram
sdram.init()

bdev = RAMDevice(512)
os.VfsFat.mkfs(bdev)
os.mount(bdev, '/ramdisk')
# now use it as usual - write / read files folders etc
```