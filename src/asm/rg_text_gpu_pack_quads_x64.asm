; rg_text_gpu - optional Windows x64 quad packing kernel
; Part of the Reverse Gravity (rg_) libraries. MIT licensed; see ../../LICENSE.
; Uses baseline SSE2 only; no AVX or additional buffer alignment is required.
; SSE2, Windows x64 ABI. Inputs and outputs may be only 4-byte aligned.
; rcx=vertices, rdx=indices, r8=quads, r9d=count, [rsp+40]=first_vertex.
; Leaf routine: only volatile GP registers and xmm0..xmm5 are modified.

.const
ALIGN 16
pack_indices0120 DWORD 0, 1, 2, 0
pack_indices2300 DWORD 2, 3, 0, 0
pack_indices_step DWORD 4, 4, 4, 4

.code
PUBLIC rg_text_gpu_pack_quads_asm
rg_text_gpu_pack_quads_asm PROC
    test r9d, r9d
    jz pack_done
    mov eax, DWORD PTR [rsp+40]
    movd xmm4, eax
    pshufd xmm4, xmm4, 0
    movdqa xmm5, xmm4
    paddd xmm4, XMMWORD PTR pack_indices0120
    paddd xmm5, XMMWORD PTR pack_indices2300
ALIGN 16
pack_loop:
    movups xmm0, XMMWORD PTR [r8]       ; x0, y0, x1, y1
    movups xmm1, XMMWORD PTR [r8+16]    ; u0, v0, u1, v1
    movups xmm2, XMMWORD PTR [r8+32]    ; r, g, b, a

    movaps xmm3, xmm2
    movss xmm3, xmm1
    shufps xmm3, xmm3, 039h            ; g, b, a, u0
    movups XMMWORD PTR [rcx+16], xmm3

    movaps xmm3, xmm1
    shufps xmm3, xmm0, 065h
    psrldq xmm3, 4                     ; v0, x1, y0, 0
    movups XMMWORD PTR [rcx+32], xmm3

    movups XMMWORD PTR [rcx+48], xmm2   ; r, g, b, a

    movaps xmm3, xmm1
    shufps xmm3, xmm0, 0e6h            ; u1, v0, x1, y1
    movups XMMWORD PTR [rcx+64], xmm3

    movaps xmm3, xmm2
    pslldq xmm3, 4                     ; 0, r, g, b
    movups XMMWORD PTR [rcx+80], xmm3

    movaps xmm3, xmm2
    shufps xmm3, xmm1, 0efh
    movss xmm3, xmm0
    shufps xmm3, xmm3, 039h            ; a, u1, v1, x0
    movups XMMWORD PTR [rcx+96], xmm3

    movaps xmm3, xmm0
    psrldq xmm3, 12
    movlhps xmm3, xmm2                 ; y1, 0, r, g
    movups XMMWORD PTR [rcx+112], xmm3

    movaps xmm3, xmm2
    shufps xmm3, xmm1, 0ceh            ; b, a, u0, v1
    movups XMMWORD PTR [rcx+128], xmm3

    movaps xmm3, xmm2
    pslldq xmm3, 4
    shufps xmm0, xmm3, 044h            ; x0, y0, 0, r
    movups XMMWORD PTR [rcx], xmm0

    movdqu XMMWORD PTR [rdx], xmm4
    movq QWORD PTR [rdx+16], xmm5
    paddd xmm4, XMMWORD PTR pack_indices_step
    paddd xmm5, XMMWORD PTR pack_indices_step
    add rcx, 144
    add rdx, 24
    add r8, 48
    dec r9d
    jnz pack_loop
pack_done:
    ret
rg_text_gpu_pack_quads_asm ENDP
END

