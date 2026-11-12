// VERSION EUROPEA (assets/eu/default.xbe, entrada 003A4D6F). Mismas funciones que src/game/ de la version en ingles,
// con las direcciones desplazadas: D3D +0x24F0 y DSOUND +0x2500. Cada una se comprobo instruccion por instruccion
// contra la original. Las direcciones del texto de los comentarios sobre el cuelgue original son las de la otra version.
#include "cpu/x86_ops.h"
#include "core/log.h"
#include "gpu/texture.h"
#include "kernel/memory.h"
#include <atomic>
#include <chrono>

X86_EXTERN_FUNC(__imp__sub_003E3DB6);

// Escribe this->[+0x10]->[+0x18]; si el puntero es NULL, no hacer nada.
X86_FUNC(sub_003E3DB6) {
    uint32_t self = X86_MEM32(ctx.esp + 4);
    if (X86_MEM32(self + 0x10) == 0) {
        ctx.eax = 0;
        ctx.esp += 12;
        return;
    }
    __imp__sub_003E3DB6(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00401B77);
X86_EXTERN_FUNC(__imp__sub_0039F240);
X86_EXTERN_FUNC(sub_0039F240);
X86_EXTERN_FUNC(sub_0038FE50);  // SetRenderTarget([C93EA0], [C93EA4]): deja el back buffer y la profundidad del juego

namespace gpu { extern std::atomic<uint32_t> g_cod_probe; extern std::atomic<int64_t> g_cod_movie_ms; extern std::atomic<uint32_t> g_cod_movie_epoch; }
static inline void XmvActive() {
    {
        const int64_t t = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (t > gpu::g_cod_movie_ms.load() + 1500) gpu::g_cod_movie_epoch++;
    }
    gpu::g_cod_movie_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 250;
}
// DIAGNOSTICO: cada 20 cuadros entregados (los primeros 8 veces por pelicula) se pide al backend una rejilla de brillo
static std::atomic<uint32_t> g_mv_frames{0}, g_mv_calls{0}, g_mv_first_hr{0xFFFFFFFFu}, g_mv_first_st{0xFFFFFFFFu};
static std::atomic<uint32_t> g_mv_end{0}, g_mv_fail{0};
static inline void XmvProbe(uint32_t& frames) {
    XmvActive();
    frames++;
    g_mv_frames++;
}

// El decoder XMV escribe los cuadros directamente en la memoria de la textura. El backend decide si debe volver a
// subir una textura de mas de 64 KB mirando solo 64 muestras de 64 bytes (MemSig en native/vulkan.cpp) y el contador
// de "sucia" (gpu::MarkDirty): un cuadro nuevo que no cambia ninguna muestra se quedaba sin subir y se veia el cuadro
// viejo o a medias. Tras cada cuadro entregado (estado 1) se marca la textura como sucia. (HIPOTESIS: parte del
// parpadeo/recorte de las cinematicas.)

// D3DDevice_Reset(pp) del juego (0x3B93F0, ret 4): ver CodResetDevice en src/d3d/d3d8.cpp
// EXPERIMENTAL, desactivado por defecto: con el HLE las cinematicas dejaron de verse (causa sin determinar).
// Se activa compilando con -DCOD_HLE_RESET.
#if defined(COD_HLE_RESET)
void CodResetDevice(X86Context& ctx);
X86_FUNC(sub_003B93F0) {
    CodResetDevice(ctx);
    ctx.eax = 0;  // D3D_OK
    ctx.esp += 8;  // direccion de retorno + 1 argumento
}
#endif

// PARCHE: si el decoder XMV no entrega cuadros durante 3 s, se da la pelicula por terminada
// (estado 2 = fin de archivo) para que el juego vuelva al menu en vez de esperar para siempre.
//
// ANDROID (__ANDROID__) o -DCOD_SKIP_MOVIES: TEMPORAL, hasta que el decoder XMV funcione en el telefono, TODAS las
// peliculas se dan por terminadas en la primera llamada (estado 2 = XMV_ENDOFFILE), sin esperar los 3 s, para que el
// juego pase directo al siguiente estado. Se sigue llamando al original para que saque la direccion de retorno y los
// argumentos de la pila como siempre (no se asume el `ret N`). En PC no cambia nada: las cinematicas se ven.
// Para volver a intentar los videos en Android basta quitar la definicion (o borrar este bloque #if).
//
// DIAGNOSTICO (-DCOD_XMV_DIAG, tiene prioridad sobre lo anterior): NO omite nada. sub_00401B77 es
// CXMVDecoder::GetNextFrame(this, surface, pxmvr, pinfo), ret 0x10 (comprobado en el codigo generado: la unica
// llamada, 0x0039FA55, empuja this/surface/&estado/0 y lee el estado en [esp+0x10]; 0=NOFRAME 1=FRAMEDECODED 2=ENDOFFILE
// 3=FAIL). Registra cada cambio de (HRESULT, estado) y las primeras llamadas, con campos del decoder y el contador de
// vblank del dispositivo (el decoder cronometra los cuadros con D3DDevice_GetDisplayFieldStatus = 0x003BA010, que
// devuelve [D3D_pDevice 0x003C9488 -> +0x1DE8]). Las ofs. de los campos [this+...] salen de lo que lee la funcion.
// Mantiene el watchdog de 3 s para que el juego no se cuelgue.


// Plays a movie as the game does (sub_0039F240, cdecl, 4 arguments). Los argumentos 2 a 4 son los que el juego pasaba
// en la unica llamada que se vio en un log (indice 3: 0, 1, 0); que significan no esta comprobado. Se llama desde el
// hilo del juego, con el contexto guardado para dejarlo como estaba.
void CodPlayMovie(X86Context& ctx, uint32_t index) {
    uint8_t* base = mem::Base();
    const X86Context saved = ctx;
    LOGI("devmovie: reproduciendo la pelicula %u", index);
    X86_PUSH32(0u);
    X86_PUSH32(1u);
    X86_PUSH32(0u);
    X86_PUSH32(index);
    X86_PUSH32(0u);  // direccion de retorno
    sub_0039F240(ctx, base);
    ctx = saved;
}

// DIAGNOSTICO (lo pide el modo de desarrollo de las cinematicas): sub_0039F240 es la rutina que reproduce una pelicula.
// Comprobado en el codigo generado: recibe 4 argumentos por la pila, el primero es un indice de 0 a 0x17 (rechaza lo
// demas) y la unica llamada, 0x00320934, empuja EAX (esi+edx), EBP, EBX y ECX. NO se sabe aun que significan los otros
// tres, por eso solo se registran en cada pelicula real; con eso se escribira el reproductor que las lanza a demanda.
X86_FUNC(sub_0039F240) {
    const uint32_t a1 = X86_MEM32(ctx.esp + 4), a2 = X86_MEM32(ctx.esp + 8), a3 = X86_MEM32(ctx.esp + 12),
                   a4 = X86_MEM32(ctx.esp + 16);
    char name[72] = {};
    const uint32_t p = X86_MEM32(0x013039E0u);
    if (p >= 0x10000u && p < 0x04000000u)
        for (int i = 0; i < 70; i++) {
            const char c = char(X86_MEM8(p + uint32_t(i)));
            if (!c) break;
            name[i] = (c >= 32 && c < 127) ? c : '?';
        }
    LOGI("playmovie: indice %u args %08X %08X %08X | [13039E0]=%08X \"%s\" [13039E4]=%08X", a1, a2, a3, a4, p, name,
         X86_MEM32(0x013039E4u));
    g_mv_frames = 0; g_mv_calls = 0; g_mv_first_hr = 0xFFFFFFFFu; g_mv_first_st = 0xFFFFFFFFu; g_mv_end = 0; g_mv_fail = 0;
    const auto t0 = std::chrono::steady_clock::now();
    __imp__sub_0039F240(ctx, base);
    const long ms = long(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count());
    // PARCHE (hipotesis apoyada por los logs): al terminar la pelicula el juego reinicia el dispositivo (D3DDevice_Reset) y el
    // render target actual queda en una superficie nueva (003CAEB4) mientras que sus punteros propios del back buffer y de la
    // profundidad ([C93EA0]/[C93EA4]) siguen siendo los de antes. Desde ese reinicio el juego ya no llama a su rutina de
    // targets fuera de pantalla (0x38FE70) y la escena se compone con un target vacio (pantalla negra). Se vuelve a
    // fijar el back buffer y la profundidad del juego, como estaban antes de la pelicula.
    {
        const X86Context saved = ctx;
        X86_PUSH32(0u);  // direccion de retorno
        sub_0038FE50(ctx, base);
        ctx = saved;
        LOGI("playmovie fin: render target del juego restaurado ([C93EA0]=%08X [C93EA4]=%08X)", X86_MEM32(0x00C93EA0u), X86_MEM32(0x00C93EA4u));
    }
    LOGI("playmovie fin: indice %u dur %ld ms | llamadas GetNextFrame %u cuadros %u fin(2) %u fallo(3) %u | primera hr=%08X st=%u",
         a1, ms, g_mv_calls.load(), g_mv_frames.load(), g_mv_end.load(), g_mv_fail.load(), g_mv_first_hr.load(), g_mv_first_st.load());
}

#if defined(COD_XMV_DIAG)
X86_FUNC(sub_00401B77) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now(), prev = clock::now();
    static uint32_t calls = 0, last_pst = 0xFFFFFFFFu, last_eax = 0xFFFFFFFFu;
    const uint32_t self = X86_MEM32(ctx.esp + 4);
    const uint32_t surf = X86_MEM32(ctx.esp + 8);
    const uint32_t pst = X86_MEM32(ctx.esp + 12);
    const auto now = clock::now();
    if (now - prev > std::chrono::seconds(1)) { last = now; calls = 0; last_pst = last_eax = 0xFFFFFFFFu; }  // pelicula nueva
    prev = now;
    __imp__sub_00401B77(ctx, base);
    const uint32_t st = X86_MEM32(pst), hr = ctx.eax;
    if (st == 1) gpu::MarkDirty(surf);  // cuadro nuevo en la textura: obliga a subirla (ver abajo)
    static uint32_t nf = 0;
    if (calls <= 1) nf = 0;
    if (st == 1) XmvProbe(nf);
    calls++;
    if (calls <= 20 || st != last_pst || hr != last_eax || calls % 300 == 0) {
        const uint32_t dev = X86_MEM32(0x003C9488u);
        LOGI("xmvdiag: #%u hr=%08X st=%u vblank=%u this+6C=%u +60=%08X +50=%u +48=%u +F8=%08X +B8=%u +BC=%u +1C=%08X +14C=%08X",
             calls, hr, st, dev ? X86_MEM32(dev + 0x1DE8u) : 0u, X86_MEM32(self + 0x6Cu), X86_MEM32(self + 0x60u),
             X86_MEM32(self + 0x50u), X86_MEM32(self + 0x48u), X86_MEM32(self + 0xF8u), X86_MEM32(self + 0xB8u),
             X86_MEM32(self + 0xBCu), X86_MEM32(self + 0x1Cu), X86_MEM32(self + 0x14Cu));
        last_pst = st;
        last_eax = hr;
    }
    if (st != 0) { last = now; return; }
    if (now - last > std::chrono::seconds(3)) {
        X86_MEM32(pst) = 2;  // XMV_ENDOFFILE
        last = now;
        LOGI("xmvdiag: sin cuadros 3 s, se fuerza el fin de la pelicula");
    }
}
#elif defined(COD_SKIP_MOVIES)
X86_FUNC(sub_00401B77) {
    const uint32_t pst = X86_MEM32(ctx.esp + 12);
    __imp__sub_00401B77(ctx, base);
    if (pst) X86_MEM32(pst) = 2;  // XMV_ENDOFFILE
    static uint32_t n = 0;
    if (n++ < 5) LOGI("diag: pelicula XMV omitida (%u)", n);
}
#else
X86_FUNC(sub_00401B77) {
    using clock = std::chrono::steady_clock;
    static clock::time_point last = clock::now(), prev = clock::now();
    const uint32_t surf = X86_MEM32(ctx.esp + 8);
    const uint32_t pst = X86_MEM32(ctx.esp + 12);
    const auto now = clock::now();
    const bool new_movie = now - prev > std::chrono::seconds(1);
    if (new_movie) last = now;  // una pelicula nueva
    prev = now;
    __imp__sub_00401B77(ctx, base);
    if (g_mv_calls++ == 0) { g_mv_first_hr = ctx.eax; g_mv_first_st = X86_MEM32(pst); }
    if (X86_MEM32(pst) == 2) g_mv_end++;
    if (X86_MEM32(pst) == 3) g_mv_fail++;
    if (X86_MEM32(pst) == 1) gpu::MarkDirty(surf);  // cuadro nuevo en la textura: obliga a subirla (ver abajo)
    static uint32_t nf = 0;
    if (new_movie) nf = 0;
    if (X86_MEM32(pst) == 1) XmvProbe(nf);
    if (X86_MEM32(pst) != 0) { last = now; return; }
    if (now - last > std::chrono::seconds(3)) {
        X86_MEM32(pst) = 2;  // XMV_ENDOFFILE
        last = now;
        LOGI("diag: xmv sin cuadros 3 s, se fuerza el fin de la pelicula");
    }
}
#endif

// ---------------------------------------------------------------------------------------------------------------
// D3DDevice_DrawIndexedVerticesUP en 0x003BCDA0.
//
//   void __stdcall DrawIndexedVerticesUP(D3DPRIMITIVETYPE prim, UINT vertexCount, const WORD* indices,
//                                        const void* vertices, UINT stride);   // ret 20
//
// El SDK no la trae. Sin override corre el codigo original del XDK, que escribe comandos de la GPU de Xbox y se
// cae dentro del helper 0x003BE8B0 (lectura en 0x4). Aqui se expanden los vertices segun los indices en un buffer
// del guest y se dibujan con D3DDevice_DrawVerticesUP (0x003BCC30), que el SDK si implementa. Una lista, tira o
// abanico indexado da el mismo resultado como secuencia de vertices expandida.
// ---------------------------------------------------------------------------------------------------------------
X86_EXTERN_FUNC(sub_003BCC30);  // D3DDevice_DrawVerticesUP (implementacion del SDK)

namespace {
uint32_t g_up_scratch = 0;        // direccion guest del buffer de expansion
uint32_t g_up_scratch_bytes = 0;  // tamano del buffer

// ExAllocatePool (ordinal 14) con un argumento stdcall. El bloque anterior, si habia, se deja: es pequeno y raro.
uint32_t UpScratch(X86Context& ctx, uint8_t* base, uint32_t bytes) {
    if (bytes > g_up_scratch_bytes) {
        uint32_t want = (bytes + 0xFFFFu) & ~0xFFFFu;
        if (want < 0x40000u) want = 0x40000u;
        X86_PUSH32(want);
        X86_PUSH32(0);  // direccion de retorno, la saca el export
        x86_kernel(ctx, base, 14);
        uint32_t p = ctx.eax;
        if (!p) return 0;
        g_up_scratch = p;
        g_up_scratch_bytes = want;
    }
    return g_up_scratch;
}
}  // namespace

X86_FUNC(sub_003BCDA0) {
    const uint32_t prim = X86_MEM32(ctx.esp + 4);
    const uint32_t count = X86_MEM32(ctx.esp + 8);
    const uint32_t indices = X86_MEM32(ctx.esp + 12);
    const uint32_t vertices = X86_MEM32(ctx.esp + 16);
    const uint32_t stride = X86_MEM32(ctx.esp + 20);

    // ret 20: direccion de retorno + 5 argumentos; se saca antes de llamar a nada que use la pila
    // (los argumentos ya estan leidos arriba)
    uint32_t dst = 0;
    if (count && stride && vertices && indices && (uint64_t)count * stride <= 0x1000000ull)
        dst = UpScratch(ctx, base, count * stride);

    if (dst) {
        for (uint32_t i = 0; i < count; i++) {
            const uint32_t src = vertices + (uint32_t)X86_MEM16(indices + i * 2u) * stride;
            const uint32_t out = dst + i * stride;
            for (uint32_t b = 0; b < stride; b++) {
                X86_MEM8(out + b) = X86_MEM8(src + b);
                X86_BARRIER();  // evita que clang convierta el bucle en una llamada a memcpy (la DLL no tiene CRT)
            }
        }
        // DrawVerticesUP(prim, count, vertices, stride): argumentos en orden inverso y una direccion de retorno
        X86_PUSH32(stride);
        X86_PUSH32(dst);
        X86_PUSH32(count);
        X86_PUSH32(prim);
        X86_PUSH32(0);
        sub_003BCC30(ctx, base);  // el SDK saca la direccion de retorno y los 4 argumentos
    }

    ctx.eax = 0;
    ctx.esp += 4 + 20;
}

// ---------------------------------------------------------------------------------------------------------------
// Metodos de DirectSound que el SDK no emula y que el juego llama. El log los lista al arrancar:
//   "dsound: exports running their original code: B0076 at 003E4A2A B005A at 003E4A86 B0066 at 003E55C2
//    B007A at 003E55E6 ..."
// Sin estos parches corre el codigo original del XDK, que escribe en un objeto de hardware inexistente y se cae
// (lectura en guest 0xB4 desde 0x003E2FDC, llamado desde 0x0039643C).
// Cada parche devuelve S_OK sin hacer nada y saca de la pila la direccion de retorno y los N bytes del `ret N`.
// El sonido de estos ajustes no se aplica: es un parche, no una emulacion.
//
// IMPORTANTE: este bloque va aqui (a mano) y NO en dsound_stubs.cpp, porque ese archivo lo regenera el script
// y se pierde lo que se escriba dentro.
// ---------------------------------------------------------------------------------------------------------------
#define DS_STUB(addr, N)    \
    X86_FUNC(sub_##addr) {  \
        ctx.eax = 0;        \
        ctx.esp += 4 + (N); \
    }

// B0076 = IDirectSoundBuffer::SetPitch (this, tono en 1/4096 de octava respecto a 48 kHz): ver CodSetPitch.
void CodSetPitch(uint32_t obj, int32_t pitch);  // src/sound/buffers.cpp
X86_FUNC(sub_003E4A2A) {
    CodSetPitch(X86_MEM32(ctx.esp + 4), int32_t(X86_MEM32(ctx.esp + 8)));
    ctx.eax = 0;
    ctx.esp += 4 + 8;
}
// B005A = IDirectSoundBuffer_PlayEx (el orden de los ids es alfabetico: B0059 es Play, B005D Release). Es la que usa el
// juego para sus efectos de sonido; con un parche vacio no suenan. (this, tiempo bajo, tiempo alto, flags), ret 0x10:
// el mismo marco de pila que Play (this, 2 reservados, flags) y los mismos bits de flags (1 bucle, 2 desde el
// inicio, 4 sincronizar), asi que se le pasa tal cual a Play (0x003E4A62), que el SDK si implementa. La hora de
// inicio se ignora: suena de inmediato.
X86_EXTERN_FUNC(sub_003E4A62);  // IDirectSoundBuffer_Play (implementacion del SDK)
X86_FUNC(sub_003E4A86) {
    sub_003E4A62(ctx, base);
}
DS_STUB(003E55C2, 12)   // B0066: ajuste float + DWORD
DS_STUB(003E55E6, 12)   // B007A: ajuste float + DWORD (el que causaba el crash al arrancar)

// ---------------------------------------------------------------------------------------------------------------
// Configuracion de una region "tile" de la GPU de Xbox, en 0x003BE6A2 (thiscall, 7 argumentos en la pila, ret 0x1C).
// Es el interior de D3DDevice_SetTile: escribe directo en los registros NV2A (PFB_TLIMIT/TSIZE en +0x100244/48 y
// PGRAPH en +0x400904/08, base = [this]) tras llamar a 0x003BE38D, que lee/escribe +0x3214, +0x3220, +0x2400...
// El SDK deja [CDevice+0] en 0 (no hay GPU real), asi que el codigo original lee la direccion guest 0x3214 y se
// cae (SIGSEGV en Android: libmain.so+0x1625620 = __imp__sub_003BE38D, llamada desde __imp__sub_003BE6A2).
// Los tiles son compresion/particionado de la memoria de la consola: aqui no hacen falta, como el HLE de
// D3DDevice_SetTile del SDK (que no se instala si el simbolo vale 0 en esta imagen).
// ---------------------------------------------------------------------------------------------------------------
DS_STUB(003BE6A2, 28)   // this + 7 argumentos (ret 0x1C, verificado en x86_recomp.118.cpp: "003BE88D ret 0x1C")

// ---------------------------------------------------------------------------------------------------------------
// Cuelgue tras saltar/terminar la pelicula: el XMV termina y el juego llama a una funcion D3D (alrededor de
// 0x003BA100) que llama a 0x003BE890, y esta a 0x003BE38D. Ambas leen/escriben registros NV2A en [CDevice+0]
// (+0x3214, +0x2400, +0x3220, +0x400750...), y esa base vale 0 en este port, asi que se cae en guest 0x3214.
//
//   0x003BE890: thiscall, 2 argumentos en la pila, ret 0x08 (verificado: "003BE94F ret 0x08"); devuelve 1 (eax=1
//               en 003BE94C). Escribe un comando de la FIFO (+0x100300/+0x400980...): no hace falta aqui.
//   0x003BE38D: espera a que la FIFO quede libre (sondea el bit 0x10), ret 0x04 (verificado en el codigo generado).
// Se dejan como parche que no hace nada, igual que 0x003BE6A2.
// ---------------------------------------------------------------------------------------------------------------
X86_FUNC(sub_003BE890) {
    LOGI("diag: sub_003BE890 omitido");
    ctx.eax = 1;
    ctx.esp += 4 + 8;
}
DS_STUB(003BE38D, 4)

// ---------------------------------------------------------------------------------------------------------------
// DIAGNOSTICO DE GAMMA (solo con -DCOD_XMV_DIAG). En Android el log muestra que el decoder XMV entrega cuadros y que
// el juego los dibuja (el render target tiene contenido), pero durante TODA la pelicula la rampa de gamma esta en cero
// (0,0,0) y la pantalla sale negra. La rampa solo la construyen dos rutinas del juego, que llaman a
// D3DDevice_SetGammaRamp (0x003B8F30):
//   0x00388E50 (sin argumentos, ret): la llama solo PlayMovie 0x0039F240 justo despues de 0x00389220(0,0), que fija
//              la escena de gamma [0x00C93E90]=0 y [0x00C93E94]=0; el preset es la fila (a*100+b) de 24 bytes
//              (6 floats) de la tabla en 0x005C4C80.
//   0x00388FE0 (1 argumento float, ret): rampa con fundido; mezcla la fila actual con la fila "escena" de 0x005CEBE4.
// Estas envolturas llaman al original y solo registran, para saber CUAL de las dos deja la rampa en cero y con que datos.
// Los floats se imprimen como bits hexadecimales.
// ---------------------------------------------------------------------------------------------------------------
#if defined(COD_XMV_DIAG)
namespace {
void GammaDiag(const char* who, uint8_t* base, uint32_t arg_bits, bool has_arg) {
    static uint32_t n = 0;
    if (n >= 400) return;
    n++;
    const uint32_t a = X86_MEM32(0x00C93E90u), b = X86_MEM32(0x00C93E94u), flag = X86_MEM32(0x005CEBE0u);
    const uint32_t idx = a * 100u + b;
    uint32_t row[6] = {0, 0, 0, 0, 0, 0};
    if (idx < 3000u)
        for (uint32_t i = 0; i < 6; i++) row[i] = X86_MEM32(0x005C4C80u + idx * 24u + i * 4u);
    LOGI("gammadiag: %s arg=%08X(%d) escena=%u,%u idx=%u flag=%u fila=%08X %08X %08X %08X %08X %08X | scratch=%08X %08X %08X %08X %08X %08X",
         who, arg_bits, int(has_arg), a, b, idx, flag, row[0], row[1], row[2], row[3], row[4], row[5],
         X86_MEM32(0x005CEBE4u), X86_MEM32(0x005CEBE8u), X86_MEM32(0x005CEBECu), X86_MEM32(0x005CEBF0u),
         X86_MEM32(0x005CEBF4u), X86_MEM32(0x005CEBF8u));
}
}  // namespace

X86_EXTERN_FUNC(__imp__sub_00388E50);
X86_FUNC(sub_00388E50) {
    GammaDiag("388E50", base, 0, false);
    __imp__sub_00388E50(ctx, base);
}

X86_EXTERN_FUNC(__imp__sub_00388FE0);
X86_FUNC(sub_00388FE0) {
    GammaDiag("388FE0", base, X86_MEM32(ctx.esp + 4), true);
    __imp__sub_00388FE0(ctx, base);
}
#endif
