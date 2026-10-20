# Draws a filled white circle in the vm simplefb.
# x1 points at WstVmConfig on entry, needs at least 201x201 pixels.
#
# Low level pseudocode, r = 100:
#   read fb_addr, fb_stride, fb_format
#   cx, cy = fb_width/2, fb_height/2
#   pixel = fb_addr + (cy - r)*fb_stride + (cx - r)*4
#   for dy = -r .. r:
#       pushqr dy
#       dy2 = dy*dy
#       for dx = -r .. r:
#           if dx*dx + dy2 <= r*r: *pixel = 0xffffffff
#           pixel += 4
#       popqr dy
#       pixel += fb_stride - (2*r + 1)*4
#   loop:
#       flush
#
# x2 is the pixel cursor, x3 is dy^2, x4 is a temporary,
# x5 is the row stride, x6 is white, x1 is dx or dy.

section .text
:_start
    # Read the handoff: pixel base, stride and format.
    ld   x2, x1, 2        # x2 = cfg.fb_addr (off 16)
    lw   x5, x1, 6        # x5 = cfg.fb_stride (off 24)
    lw   x3, x1, 7        # x3 = cfg.fb_format (off 28)
    addi x6, x0, 1        # WST_VM_FB_FORMAT_RGBA8888
    bne  x3, x6, :bad_format

    # Reject sizes that cannot hold the 201x201 bounding box.
    lw   x3, x1, 2        # x3 = cfg.fb_width (off 8)
    lw   x4, x1, 3        # x4 = cfg.fb_height (off 12)
    addi x6, x0, 201
    blt  x3, x6, :too_small
    blt  x4, x6, :too_small

    # Center the bounding box: x3 = cx-r, x4 = cy-r.
    srai x3, x3, 1
    srai x4, x4, 1
    addi x3, x3, -100
    addi x4, x4, -100

    # x2 = first pixel of the top row.
    mul  x4, x4, x5
    add  x2, x2, x4
    slli x3, x3, 2
    add  x2, x2, x3

    addi x6, x0, -1       # white RGBA
    addi x1, x0, -100     # dy = -r

:row
    # Keep dy on the stack, then walk dx across the row.
    pushqr x1
    mul  x3, x1, x1       # dy^2
    addi x1, x0, -100     # dx = -r

:col
    # Pixel test: dx^2 + dy^2 <= r^2.
    mul  x4, x1, x1       # dx^2
    add  x4, x4, x3
    sltiu x4, x4, 10001
    beq  x4, x0, :skip
    sw   x2, x6, 0        # inside: white pixel
:skip
    addi x2, x2, 4        # next pixel
    addi x1, x1, 1
    addi x4, x0, 101
    blt  x1, x4, :col

    # Next row: one stride down, undo the 201 pixel steps.
    popqr x1
    add  x2, x2, x5
    addi x2, x2, -804
    addi x1, x1, 1
    addi x4, x0, 101
    blt  x1, x4, :row

:done
    # Frame loop: flush yields until the host presents the frame.
    addi x1, x0, 2        # WST_VM_SYS_FLUSH
    ecall
    beq  x0, x0, :done

:too_small
    hlt  2

:bad_format
    hlt  1
