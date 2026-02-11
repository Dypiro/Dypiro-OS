global gdt_flush
global gdt_flush
gdt_flush:
    lgdt [rdi]        ; Load the GDT pointer
    push 0x08         ; New Code Segment (index 1 * 8)
    lea rax, [rel .reload] 
    push rax
    retfq             ; This "Far Return" is what actually updates CS
.reload:
    mov ax, 0x10      ; New Data Segment (index 2 * 8)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret
.reload_segments:
    mov ax, 0x10      ; 0x10 is Data Segment (Entry 2 * 8 bytes)
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ret