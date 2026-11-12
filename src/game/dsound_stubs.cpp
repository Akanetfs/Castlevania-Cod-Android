#include "cpu/x86_ops.h"

// VERSION EUROPEA (assets/eu/default.xbe, entrada 003A4D6F). Mismas funciones que src/game/ de la version en ingles,
// con las direcciones desplazadas: D3D +0x24F0 y DSOUND +0x2500. Cada una se comprobo instruccion por instruccion
// contra la original. Las direcciones del texto de los comentarios sobre el cuelgue original son las de la otra version.

// GENERADO por dsound_vtables.py: métodos de DirectSound que el juego usa y el
// SDK no emula. Devuelven S_OK sin hacer nada. Es un parche para poder arrancar.

X86_FUNC(sub_003E38CC) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E38D9) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E38EA) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E3F21) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E3F68) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E4854) {  // ret 8
    ctx.eax = 0;
    ctx.esp += 12;
}

X86_FUNC(sub_003E485E) {  // ret 8
    ctx.eax = 0;
    ctx.esp += 12;
}

X86_FUNC(sub_003E4D6C) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E4F4F) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E51CA) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

// 0x003E312A: no pude determinar `ret N`, revisar a mano

X86_FUNC(sub_003E5D78) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E6006) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E6588) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E83AB) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E8BCF) {  // ret 16
    ctx.eax = 0;
    ctx.esp += 20;
}

X86_FUNC(sub_003E8BFE) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E90C5) {  // ret 0
    ctx.eax = 0;
    ctx.esp += 4;
}

// 0x003E6CAA: no pude determinar `ret N`, revisar a mano

X86_FUNC(sub_003E93F5) {  // ret 8
    ctx.eax = 0;
    ctx.esp += 12;
}

X86_FUNC(sub_003E9440) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E99A8) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E9AAE) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003E9C01) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

// 0x003E774C: no pude determinar `ret N`, revisar a mano

X86_FUNC(sub_003EB273) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003EB79F) {  // ret 0
    ctx.eax = 0;
    ctx.esp += 4;
}

X86_FUNC(sub_003EB881) {  // ret 16
    ctx.eax = 0;
    ctx.esp += 20;
}

X86_FUNC(sub_003EB8B0) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

X86_FUNC(sub_003EBCFE) {  // ret 0
    ctx.eax = 0;
    ctx.esp += 4;
}

X86_FUNC(sub_003EBE18) {  // ret 8
    ctx.eax = 0;
    ctx.esp += 12;
}

X86_FUNC(sub_003EBE67) {  // ret 4
    ctx.eax = 0;
    ctx.esp += 8;
}

