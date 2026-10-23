#include "model/control/WeiBenchmark.h"
#include "model/control/WeiScoring.h"

#include <QtConcurrent>
#include <QFutureWatcher>
#include <QTimer>
#include <QThread>
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QElapsedTimer>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QOpenGLFramebufferObject>
#include <QOpenGLShaderProgram>

#include <vector>
#include <thread>
#include <atomic>
#include <cstring>
#include <cmath>

#include <zlib.h>
#include <openssl/evp.h>

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>

namespace ControlPanel {

#if defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#  define WEI_HAVE_SSE2 1
#endif

// WeiResult: scoring + persistence

void WeiResult::rescore()
{
    memoryScore   = Wei::scoreFromTable(memoryBandwidthMBs, Wei::kMemory);
    cpuScore      = Wei::scoreFromTable(cpuThroughputMBs,   Wei::kCpu);
    diskScore     = Wei::scoreFromTable(diskReadMBs,        Wei::kDisk);
    graphicsScore = Wei::scoreFromTable(graphicsFps,        Wei::kGraphics);
    gamingScore   = Wei::scoreFromTable(gamingFps,          Wei::kGaming);

    // Base = minimum of the *assessed* subscores (a 0.0 score means a component
    // could not be measured and is excluded rather than dragging the base to 0)
    double base = 0.0;
    bool first = true;
    const double scores[] = { memoryScore, cpuScore, diskScore, graphicsScore, gamingScore };
    for (double score : scores) {
        if (score <= 0.0)
            continue;
        if (first || score < base) {
            base = score;
            first = false;
        }
    }
    baseScore = base;
}

QString WeiResult::storePath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.local/share/control-panel");
    return dir + QStringLiteral("/winsat-datastore.ini");
}

bool WeiResult::save() const
{
    const QString path = storePath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue("memoryBandwidthMBs", memoryBandwidthMBs);
    settings.setValue("cpuThroughputMBs",   cpuThroughputMBs);
    settings.setValue("diskReadMBs",        diskReadMBs);
    settings.setValue("graphicsFps",        graphicsFps);
    settings.setValue("gamingFps",          gamingFps);
    settings.setValue("assessedAt",         assessedAt);
    settings.sync();
    return settings.status() == QSettings::NoError;
}

WeiResult WeiResult::load()
{
    WeiResult r;
    QSettings settings(storePath(), QSettings::IniFormat);
    if (!settings.contains("memoryBandwidthMBs"))
        // R.valid stays false
        return r;
    r.memoryBandwidthMBs = settings.value("memoryBandwidthMBs").toDouble();
    r.cpuThroughputMBs   = settings.value("cpuThroughputMBs").toDouble();
    r.diskReadMBs        = settings.value("diskReadMBs").toDouble();
    r.graphicsFps        = settings.value("graphicsFps").toDouble();
    r.gamingFps          = settings.value("gamingFps").toDouble();
    r.assessedAt         = settings.value("assessedAt").toDateTime();
    r.rescore();
    r.valid = true;
    return r;
}

// Benchmarks

namespace {
int hwThreads()
{
    const unsigned n = std::thread::hardware_concurrency();
    if (n)
        return static_cast<int>(n);
    return 4;
}
} // namespace

namespace {
#ifdef WEI_HAVE_SSE2
// Streaming copy with 128-bit non-temporal stores (the SSE2 baseline that every
// x86-64 part has). Aligned loads paired with cache-bypassing stores, so the
// figure reflects DRAM write bandwidth, not cache throughput
void copySSE2(char *dst, const char *src, size_t bytes)
{
    auto *to = reinterpret_cast<__m128i *>(dst);
    auto *from = reinterpret_cast<const __m128i *>(src);
    const size_t n = bytes / sizeof(__m128i);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        _mm_prefetch(reinterpret_cast<const char *>(from + i + 64), _MM_HINT_NTA);
        for (int k = 0; k < 8; ++k)
            _mm_stream_si128(to + i + k, _mm_load_si128(from + i + k));
    }
    for (; i < n; ++i)
        _mm_stream_si128(to + i, _mm_load_si128(from + i));
    _mm_sfence();
}

// 256-bit AVX2 variant: wider non-temporal stores reach higher write-combining
// throughput. Compiled for AVX2 regardless of global flags and selected only
// when the running CPU reports support
__attribute__((target("avx2")))
void copyAVX2(char *dst, const char *src, size_t bytes)
{
    auto *to = reinterpret_cast<__m256i *>(dst);
    auto *from = reinterpret_cast<const __m256i *>(src);
    const size_t n = bytes / sizeof(__m256i);
    size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        _mm_prefetch(reinterpret_cast<const char *>(from + i + 64), _MM_HINT_NTA);
        for (int k = 0; k < 8; ++k)
            _mm256_stream_si256(to + i + k, _mm256_load_si256(from + i + k));
    }
    for (; i < n; ++i)
        _mm256_stream_si256(to + i, _mm256_load_si256(from + i));
    _mm_sfence();
}
#endif

// Mirrors WinSAT's MemCopy_128_SSE_UCW kernel; picks the widest available
// non-temporal path at runtime, falling back to memcpy off-x86
void streamingCopy(char *dst, const char *src, size_t bytes)
{
#ifdef WEI_HAVE_SSE2
    if (__builtin_cpu_supports("avx2"))
        copyAVX2(dst, src, bytes);
    else
        copySSE2(dst, src, bytes);
#else
    std::memcpy(dst, src, bytes);
#endif
}
} // namespace

// Memory: each thread streams its own buffer pair for a fixed window. We report
// one-direction copy throughput in MiB/s, WinSAT's exact convention (its
// "Bandwidth" field equals bytes copied / 1048576 / seconds). WinSAT runs one
// thread per physical core, so we use half the logical CPUs
double WeiBenchmark::benchMemory()
{
    const unsigned hw = std::thread::hardware_concurrency();
    // Four when the system cannot say how many it has
    int threads = 4;
    if (hw > 0)
        threads = std::max(1u, hw / 2);
    // WinSAT splits a 32 MiB block across its threads so each thread's source
    // buffer stays resident in cache: reads come from cache, only the streaming
    // stores reach DRAM, so the reported copy rate tracks DRAM write bandwidth.
    // A per-thread buffer that spills to DRAM on both sides would read ~half.
    // Small enough to stay cache-resident
    const size_t bufBytes = 1ULL * 1024 * 1024;
    const double seconds = 1.0;

    std::atomic<unsigned long long> totalCopied{0};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int worker = 0; worker < threads; ++worker) {
        pool.emplace_back([&]() {
            void *source = nullptr;
            void *target = nullptr;
            const bool allocated = posix_memalign(&source, 64, bufBytes) == 0
                && posix_memalign(&target, 64, bufBytes) == 0;
            if (!allocated) {
                free(source);
                free(target);
                return;
            }
            std::memset(source, 1, bufBytes);
            std::memset(target, 2, bufBytes);
            QElapsedTimer timer;
            timer.start();
            unsigned long long copied = 0;
            while (timer.nsecsElapsed() < seconds * 1e9) {
                streamingCopy(static_cast<char *>(target),
                              static_cast<char *>(source), bufBytes);
                copied += bufBytes;
            }
            totalCopied += copied;
            free(source);
            free(target);
        });
    }
    for (auto &th : pool)
        th.join();

    // MiB/s, one-direction
    return double(totalCopied) / (1024.0 * 1024.0) / seconds;
}

// CPU: every core runs a mix of zlib compression and AES-256 encryption for a
// fixed window; we report the aggregate payload throughput in MB/s
double WeiBenchmark::benchCpu()
{
    const int threads = hwThreads();
    const double seconds = 1.0;
    // 1 MiB payload per pass
    const size_t block = 1ULL * 1024 * 1024;

    std::atomic<unsigned long long> totalBytes{0};
    std::vector<std::thread> pool;
    pool.reserve(threads);
    for (int worker = 0; worker < threads; ++worker) {
        pool.emplace_back([&, worker]() {
            // Semi-compressible payload: a repeating pattern perturbed per byte
            // so the compressor has real work to do (not all-zeros, not noise)
            std::vector<unsigned char> src(block);
            for (size_t i = 0; i < block; ++i)
                src[i] = static_cast<unsigned char>((i * 1103515245u + worker) >> 7);
            std::vector<unsigned char> comp(compressBound(block));
            std::vector<unsigned char> enc(block + 32);

            unsigned char key[32];
            unsigned char iv[16];
            std::memset(key, 0x2b, sizeof key);
            std::memset(iv,  0x7e, sizeof iv);
            EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();

            QElapsedTimer timer;
            timer.start();
            unsigned long long bytes = 0;
            while (timer.nsecsElapsed() < seconds * 1e9) {
                // Compress
                uLongf clen = comp.size();
                compress2(comp.data(), &clen, src.data(), block, 1);
                bytes += block;
                // Encrypt
                int outl = 0, tmpl = 0;
                EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key, iv);
                EVP_EncryptUpdate(ctx, enc.data(), &outl, src.data(), int(block));
                EVP_EncryptFinal_ex(ctx, enc.data() + outl, &tmpl);
                bytes += block;
            }
            EVP_CIPHER_CTX_free(ctx);
            totalBytes += bytes;
        });
    }
    for (auto &th : pool)
        th.join();

    return double(totalBytes) / (1024.0 * 1024.0) / seconds;
}

// Disk: write a sizeable file, then read it back sequentially with O_DIRECT so
// the page cache can't serve it (no root needed). Falls back to a posix_fadvise
// cache-drop if the filesystem rejects O_DIRECT
double WeiBenchmark::benchDisk(QString *err)
{
    auto fail = [&](const QString &message) -> double {
        if (err)
            *err = message;
        return 0.0;
    };

    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (dir.isEmpty())
        dir = QDir::homePath() + QStringLiteral("/.cache");
    QDir().mkpath(dir);
    const QByteArray path =
        (dir + QStringLiteral("/wei-disktest.tmp")).toLocal8Bit();

    // 512 MiB
    const size_t fileBytes = 512ULL * 1024 * 1024;
    // 8 MiB, 4K-aligned
    const size_t chunk     = 8ULL * 1024 * 1024;

    // Write the test file (buffered) and flush it to the platter
    {
        int fd = ::open(path.constData(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0)
            return fail(QStringLiteral("cannot create test file"));
        // Incompressible payload: transparent-compression filesystems (e.g.
        // btrfs with compress=zstd) would otherwise shrink a uniform buffer to
        // nothing and the "read" would never touch the device. A cheap xorshift
        // PRNG produces bytes zstd can't squeeze
        std::vector<char> buf(chunk);
        {
            unsigned long long x = 0x9e3779b97f4a7c15ULL;
            auto *words = reinterpret_cast<unsigned long long *>(buf.data());
            for (size_t i = 0; i < chunk / sizeof(x); ++i) {
                // xorshift64, three shifts per number
                x ^= x << 13;
                x ^= x >> 7;
                x ^= x << 17;
                words[i] = x;
            }
        }
        size_t written = 0;
        while (written < fileBytes) {
            ssize_t n = ::write(fd, buf.data(), chunk);
            if (n <= 0) { ::close(fd); ::unlink(path.constData());
                          return fail(QStringLiteral("write failed")); }
            written += size_t(n);
        }
        ::fsync(fd);
        ::close(fd);
    }

    // Read it back, bypassing the cache
    void *aligned = nullptr;
    if (posix_memalign(&aligned, 4096, chunk) != 0)
        { ::unlink(path.constData()); return fail(QStringLiteral("alloc failed")); }

    bool direct = true;
    int fd = ::open(path.constData(), O_RDONLY | O_DIRECT);
    // FS doesn't support O_DIRECT
    if (fd < 0 && errno == EINVAL) {
        direct = false;
        fd = ::open(path.constData(), O_RDONLY);
    }
    if (fd < 0) { free(aligned); ::unlink(path.constData());
                  return fail(QStringLiteral("cannot reopen test file")); }
    if (!direct)
        ::posix_fadvise(fd, 0, fileBytes, POSIX_FADV_DONTNEED);

    QElapsedTimer timer;
    timer.start();
    size_t readBytes = 0;
    for (;;) {
        ssize_t n = ::read(fd, aligned, chunk);
        if (n <= 0)
            break;
        readBytes += size_t(n);
    }
    const double secs = timer.nsecsElapsed() / 1e9;
    ::close(fd);
    free(aligned);
    ::unlink(path.constData());

    if (secs <= 0.0 || readBytes == 0)
        return fail(QStringLiteral("read produced no data"));
    if (err && !direct)
        *err = QStringLiteral("O_DIRECT unavailable, figure may be cache-inflated");
    return double(readBytes) / (1024.0 * 1024.0) / secs;
}

// OpenGL passes (run on the GUI thread)

namespace {
// Render `work` units of fragment load per frame to an offscreen FBO for a
// fixed wall-clock window, returning fps. `frag` is the fragment shader body.
// Returns 0.0 if a GL context can't be brought up (e.g. headless)
double glFps(const char *vertSrc, const char *fragSrc,
             int instanceCount, bool depth, double seconds)
{
    QOffscreenSurface surface;
    surface.create();
    if (!surface.isValid())
        return 0.0;

    QOpenGLContext ctx;
    if (!ctx.create() || !ctx.makeCurrent(&surface))
        return 0.0;

    QOpenGLExtraFunctions f(&ctx);
    f.initializeOpenGLFunctions();

    const int width = 1280;
    const int height = 720;
    auto attachment = QOpenGLFramebufferObject::NoAttachment;
    if (depth)
        attachment = QOpenGLFramebufferObject::Depth;
    QOpenGLFramebufferObject fbo(width, height, attachment);
    if (!fbo.isValid()) {
        ctx.doneCurrent();
        return 0.0;
    }
    fbo.bind();

    QOpenGLShaderProgram prog;
    if (!prog.addShaderFromSourceCode(QOpenGLShader::Vertex, vertSrc) ||
        !prog.addShaderFromSourceCode(QOpenGLShader::Fragment, fragSrc) ||
        !prog.link()) {
        ctx.doneCurrent();
        return 0.0;
    }
    prog.bind();

    f.glViewport(0, 0, width, height);
    // The depth test for the gaming score, blending for the graphics one
    GLbitfield clearBits = GL_COLOR_BUFFER_BIT;
    if (depth) {
        f.glEnable(GL_DEPTH_TEST);
        clearBits |= GL_DEPTH_BUFFER_BIT;
    } else {
        f.glEnable(GL_BLEND);
        f.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    const int tLoc = prog.uniformLocation("uT");

    QElapsedTimer timer;
    timer.start();
    long frames = 0;
    while (timer.nsecsElapsed() < seconds * 1e9) {
        f.glClear(clearBits);
        if (tLoc >= 0)
            prog.setUniformValue(tLoc, float(frames) * 0.01f);
        // Each frame draws `instanceCount` full-viewport triangles; the GPU
        // generates positions from gl_VertexID, so no vertex buffers needed
        f.glDrawArraysInstanced(GL_TRIANGLES, 0, 3, instanceCount);
        f.glFinish();
        ++frames;
    }
    const double secs = timer.nsecsElapsed() / 1e9;

    prog.release();
    fbo.release();
    ctx.doneCurrent();
    if (secs > 0.0)
        return frames / secs;
    return 0.0;
}
} // namespace

// Graphics (Aero / 2D): heavy alpha-blended overdraw, many translucent
// full-screen layers composited per frame, the way the DWM stacks windows
double WeiBenchmark::benchGraphics()
{
    static const char *vert =
        "#version 330 core\n"
        "uniform float uT;\n"
        "out vec2 vUV;\n"
        "void main(){\n"
        "  vec2 p = vec2((gl_VertexID==1)?3.0:-1.0,(gl_VertexID==2)?3.0:-1.0);\n"
        "  vUV = p*0.5+0.5; gl_Position = vec4(p,0.0,1.0);\n"
        "}\n";
    static const char *frag =
        "#version 330 core\n"
        "in vec2 vUV; out vec4 o; uniform float uT;\n"
        "void main(){\n"
        "  float a = 0.04;\n"
        "  vec3 c = 0.5+0.5*cos(uT+vUV.xyx*6.0+vec3(0,2,4));\n"
        "  o = vec4(c, a);\n"
        "}\n";
    // 400 translucent full-viewport layers per frame, DWM-class overdraw
    return glFps(vert, frag, /*instanceCount=*/400, /*depth=*/false, 1.5);
}

// Gaming (3D): a depth-tested mass of shaded triangles spread through the
// frustum, a stand-in for a Direct3D scene
double WeiBenchmark::benchGaming()
{
    static const char *vert =
        "#version 330 core\n"
        "uniform float uT; out vec3 vC;\n"
        "void main(){\n"
        "  int tri = gl_InstanceID; int v = gl_VertexID;\n"
        "  float a = float(tri)*0.1 + uT;\n"
        "  float z = fract(float(tri)*0.013);\n"
        "  vec2 base = vec2(cos(a),sin(a))*0.8;\n"
        "  vec2 off = vec2((v==1)?0.05:-0.05,(v==2)?0.05:-0.05);\n"
        "  vC = 0.5+0.5*vec3(sin(a),cos(a),sin(z*6.0));\n"
        "  gl_Position = vec4(base+off, z, 1.0);\n"
        "}\n";
    static const char *frag =
        "#version 330 core\n"
        "in vec3 vC; out vec4 o;\n"
        "void main(){ o = vec4(vC,1.0); }\n";
    // 20k depth-tested triangles per frame
    return glFps(vert, frag, /*instanceCount=*/20000, /*depth=*/true, 1.5);
}

// Orchestration

WeiBenchmark::WeiBenchmark(QObject *parent) : QObject(parent) {}
WeiBenchmark::~WeiBenchmark() = default;

void WeiBenchmark::run()
{
    if (m_running)
        return;
    m_running = true;
    m_result = WeiResult{};
    m_phase = Phase::Cpu;
    emit progress(0, QStringLiteral("Assessing processor…"));
    startPhase();
}

void WeiBenchmark::startPhase()
{
    switch (m_phase) {
    case Phase::Cpu:
    case Phase::Memory:
    case Phase::Disk:
        runWorkerPhase();
        break;
    case Phase::Graphics:
    case Phase::Gaming:
        runGlPhase();
        break;
    case Phase::Done: {
        m_result.assessedAt = QDateTime::currentDateTime();
        m_result.rescore();
        m_result.valid = true;
        m_result.save();
        m_running = false;
        emit progress(100, QStringLiteral("Assessment complete"));
        emit finished(m_result);
        break;
    }
    case Phase::Idle:
        break;
    }
}

void WeiBenchmark::runWorkerPhase()
{
    Phase phase = m_phase;
    auto *watcher = new QFutureWatcher<double>(this);
    m_watcher = watcher;

    connect(watcher, &QFutureWatcher<double>::finished, this, [this, watcher, phase]() {
        const double measured = watcher->result();
        switch (phase) {
        case Phase::Cpu:
            m_result.cpuThroughputMBs = measured;
            m_phase = Phase::Memory;
            emit progress(20, QStringLiteral("Assessing memory (RAM)…"));
            break;
        case Phase::Memory:
            m_result.memoryBandwidthMBs = measured;
            m_phase = Phase::Disk;
            emit progress(40, QStringLiteral("Assessing primary hard disk…"));
            break;
        case Phase::Disk:
            m_result.diskReadMBs = measured;
            m_phase = Phase::Graphics;
            emit progress(60, QStringLiteral("Assessing desktop graphics…"));
            break;
        default:
            break;
        }
        watcher->deleteLater();
        if (m_watcher == watcher)
            m_watcher = nullptr;
        QTimer::singleShot(0, this, [this]() { startPhase(); });
    });

    QFuture<double> fut;
    switch (phase) {
    case Phase::Cpu:
        fut = QtConcurrent::run(&WeiBenchmark::benchCpu);
        break;
    case Phase::Memory:
        fut = QtConcurrent::run(&WeiBenchmark::benchMemory);
        break;
    case Phase::Disk:
        fut = QtConcurrent::run([]() { return benchDisk(nullptr); });
        break;
    default:
        break;
    }
    watcher->setFuture(fut);
}

void WeiBenchmark::runGlPhase()
{
    Phase phase = m_phase;
    // Defer the (blocking) GL work one tick so the progress label paints first
    QTimer::singleShot(0, this, [this, phase]() {
        if (phase == Phase::Graphics) {
            m_result.graphicsFps = benchGraphics();
            m_phase = Phase::Gaming;
            emit progress(80, QStringLiteral("Assessing 3D gaming graphics…"));
        // Gaming
        } else {
            m_result.gamingFps = benchGaming();
            m_phase = Phase::Done;
            emit progress(95, QStringLiteral("Computing scores…"));
        }
        QTimer::singleShot(0, this, [this]() { startPhase(); });
    });
}

} // namespace ControlPanel
