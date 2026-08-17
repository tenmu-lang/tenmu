// std/mem.tm — アロケータ (Tenmu実装)
// 「アロケータは値である」という設計方針(tenmu-spec.md §1.2)の中核。
// PageAllocatorはstd.os.mem上に構築するホスト型向け実装。
// BumpAllocatorはOS呼び出し無しで動くフリースタンディング向け実装。

module std.mem

import std.os.mem as osmem

error AllocError {
    OutOfMemory,
    InvalidLayout,
}

trait Allocator {
    fn alloc(&mut self, size: usize, align: usize) -> Result<*mut u8, AllocError>
    fn free(&mut self, ptr: *mut u8, size: usize, align: usize)
    fn realloc(&mut self, ptr: *mut u8, old_size: usize, new_size: usize, align: usize) -> Result<*mut u8, AllocError> {
        let new_ptr = self.alloc(new_size, align)?
        unsafe {
            osmem.copy(new_ptr, ptr, if old_size < new_size { old_size } else { new_size })
        }
        self.free(ptr, old_size, align)
        return Ok(new_ptr)
    }
}

// OSのページ確保に直接乗る汎用アロケータ(ホスト型の既定アロケータ)
struct PageAllocator {}

impl Allocator for PageAllocator {
    fn alloc(&mut self, size: usize, align: usize) -> Result<*mut u8, AllocError> {
        let p = osmem.os_alloc(size, align)
        if p == null {
            return Err(AllocError.OutOfMemory)
        }
        return Ok(p)
    }
    fn free(&mut self, ptr: *mut u8, size: usize, align: usize) {
        osmem.os_free(ptr, size, align)
    }
}

// 固定バッファの中を先頭から積んでいくだけの単純なアロケータ。
// 個別のfreeはできない(reset()で一括解放)。フリースタンディングでも使える。
struct BumpAllocator {
    buffer: *mut u8,
    capacity: usize,
    offset: usize,
}

impl BumpAllocator {
    fn from_buffer(buffer: *mut u8, capacity: usize) -> BumpAllocator {
        return BumpAllocator { buffer: buffer, capacity: capacity, offset: 0 }
    }

    fn reset(&mut self) {
        self.offset = 0
    }
}

impl Allocator for BumpAllocator {
    fn alloc(&mut self, size: usize, align: usize) -> Result<*mut u8, AllocError> {
        let aligned = (self.offset + align - 1) / align * align
        if aligned + size > self.capacity {
            return Err(AllocError.OutOfMemory)
        }
        self.offset = aligned + size
        unsafe {
            return Ok(self.buffer.add(aligned))
        }
    }
    fn free(&mut self, ptr: *mut u8, size: usize, align: usize) {
        // BumpAllocatorは個別解放をサポートしない(意図的な no-op)
    }
}
