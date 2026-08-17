#[target(x86_64-freestanding)]
module kernel

import std.os.mem

extern "C" fn _start() -> ! {
    let vga: *mut u16 = mem.mmio(0xB8000)
    unsafe {
        *vga = 0x0F41
    }
    loop {}
}
