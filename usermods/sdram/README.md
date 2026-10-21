# SDRAM as block device

For temporary storage. By default this retains the original layout and
`preallocated_ptr()` API. Builds that set `F469_SDRAM_LAYOUT=1` use the RAM
disk region assigned by a `sdram_layout.h` supplied on the compiler include
path.

Usage:

```py
import sdram
sdram.init()

bdev = RAMDevice(512)
os.VfsFat.mkfs(bdev)
os.mount(bdev, '/ramdisk')
# now use it as usual - write / read files folders etc
```