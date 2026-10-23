#pragma once

#include <QObject>
#include <QString>
#include <QDateTime>

class QFutureWatcherBase;
template <typename T> class QFutureWatcher;

namespace ControlPanel {

// One full Windows-Experience-Index assessment: the five raw metrics, the five
// derived subscores, and the base score (the minimum of the subscores)
struct WeiResult {
    // Raw metrics (the calibration-grade, scale-independent numbers)
    // Streaming copy, one-direction MiB/s (WinSAT convention)
    double memoryBandwidthMBs = 0.0;
    // Compression + encryption, all cores
    double cpuThroughputMBs   = 0.0;
    // Sequential read
    double diskReadMBs        = 0.0;
    // 2D / Aero compositing
    double graphicsFps        = 0.0;
    // 3D scene
    double gamingFps          = 0.0;

    // Subscores (1.0-7.9; 0.0 means "not assessed")
    double memoryScore   = 0.0;
    double cpuScore      = 0.0;
    double diskScore     = 0.0;
    double graphicsScore = 0.0;
    double gamingScore   = 0.0;
    // Min of the assessed subscores
    double baseScore     = 0.0;

    QDateTime assessedAt;
    bool valid = false;

    // Recompute subscores + base from the raw metrics via the scoring tables
    void rescore();

    // Persist to / load from the on-disk data store (our DataStore analogue)
    bool save() const;
    static WeiResult load();
    static QString storePath();
};

// Runs the assessment. CPU/memory/disk run on a worker thread (QtConcurrent);
// the OpenGL graphics/gaming passes run on the GUI thread between them, so the
// whole thing is driven as a small state machine that keeps the UI responsive
// and emits progress as it goes
class WeiBenchmark : public QObject {
    Q_OBJECT
public:
    explicit WeiBenchmark(QObject *parent = nullptr);
    ~WeiBenchmark() override;

    // Start a fresh assessment
    void run();
    bool isRunning() const { return m_running; }

signals:
    void progress(int percent, const QString &phase);
    void finished(const WeiResult &result);

private:
    // Advance the state machine
    void startPhase();
    // Dispatch the current CPU/mem/disk metric
    void runWorkerPhase();
    // Run the current GL metric inline
    void runGlPhase();

    // Individual benchmarks (return the raw metric)
    // MB/s, r+w convention
    static double benchMemory();
    // MB/s aggregate
    static double benchCpu();
    // MB/s sequential read
    static double benchDisk(QString *err);
    // Fps (GUI thread)
    double benchGraphics();
    // Fps (GUI thread)
    double benchGaming();

    enum class Phase { Idle, Cpu, Memory, Disk, Graphics, Gaming, Done };
    Phase m_phase = Phase::Idle;
    bool  m_running = false;
    WeiResult m_result;
    QFutureWatcher<double> *m_watcher = nullptr;
};

} // namespace ControlPanel
