#ifndef SIGN_LANGUAGE_QML_SIGN_CONTROLLER_HPP
#define SIGN_LANGUAGE_QML_SIGN_CONTROLLER_HPP

#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <thread>

class CameraView;

class SignController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentWord READ currentWord NOTIFY currentWordChanged)
    Q_PROPERTY(QString confidenceText READ confidenceText NOTIFY metricsChanged)
    Q_PROPERTY(QString fpsText READ fpsText NOTIFY metricsChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString latencyText READ latencyText NOTIFY metricsChanged)
    Q_PROPERTY(QString tokensText READ tokensText NOTIFY tokensChanged)
    Q_PROPERTY(QString sentenceText READ sentenceText NOTIFY sentenceChanged)
    Q_PROPERTY(QString composerText READ composerText NOTIFY sentenceChanged)
    Q_PROPERTY(int collectProgress READ collectProgress NOTIFY progressChanged)
    Q_PROPERTY(bool running READ running NOTIFY runningChanged)

public:
    explicit SignController(QObject* parent = nullptr);
    ~SignController() override;
    void setCameraView(CameraView* cameraView);

    QString currentWord() const { return current_word_; }
    QString confidenceText() const { return confidence_text_; }
    QString fpsText() const { return fps_text_; }
    QString statusText() const { return status_text_; }
    QString latencyText() const { return latency_text_; }
    QString tokensText() const { return tokens_text_; }
    QString sentenceText() const { return sentence_text_; }
    QString composerText() const { return composer_text_; }
    int collectProgress() const { return collect_progress_; }
    bool running() const { return running_; }

    Q_INVOKABLE void startSystem();
    Q_INVOKABLE void stopSystem();
    Q_INVOKABLE void startRecording();
    Q_INVOKABLE void stopRecording();
    Q_INVOKABLE void clearExpression();

signals:
    void currentWordChanged();
    void metricsChanged();
    void statusChanged();
    void tokensChanged();
    void sentenceChanged();
    void progressChanged();
    void runningChanged();
    void logLine(const QString& line);

private slots:
    void readStdout();
    void readStderr();
    void updateLocalPreview();
    void logDiagnostics();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);

private:
    void appendLog(const QString& line);
    void consumeStdoutLine(const QByteArray& line);
    void consumeJson(const QJsonObject& object);
    QStringList jsonTokens(const QJsonArray& tokens) const;
    QString tokensDisplayText(const QJsonArray& tokens) const;
    void startLocalPreview();
    void stopLocalPreview();
    void capturePreviewFrames(int cameraId, int width, int height);
    void sendWorkerCommand(const QString& command);
    QString projectPath(const QString& relativePath) const;
    QStringList workerArguments() const;
    void setRunning(bool running);
    void setStatus(const QString& status);
    void setProgress(int progress);

    QProcess worker_;
    CameraView* camera_view_ = nullptr;
    QTimer preview_timer_;
    QTimer diagnostics_timer_;
    std::thread preview_thread_;
    std::mutex preview_mutex_;
    QByteArray latest_worker_frame_line_;
    int latest_worker_frame_id_ = 0;
    int sent_worker_frame_id_ = 0;
    int latest_worker_frame_width_ = 0;
    int latest_worker_frame_height_ = 0;
    std::atomic_bool preview_stop_{false};
    std::atomic_bool preview_update_pending_{false};
    std::atomic<unsigned long long> capture_frames_{0};
    std::atomic<unsigned long long> preview_delivered_{0};
    std::atomic<unsigned long long> preview_dropped_{0};
    std::atomic<unsigned long long> worker_frames_encoded_{0};
    std::atomic<unsigned long long> worker_frames_sent_{0};
    std::atomic<unsigned long long> worker_frames_backpressure_{0};
    std::atomic<unsigned long long> worker_results_{0};
    bool local_preview_active_ = false;
    bool has_collected_tokens_ = false;
    QByteArray stdout_buffer_;
    QByteArray stderr_buffer_;

    QString current_word_ = QStringLiteral("等待中...");
    QString confidence_text_ = QStringLiteral("置信度 -");
    QString fps_text_ = QStringLiteral("FPS -");
    QString status_text_ = QStringLiteral("未启动");
    QString latency_text_ = QStringLiteral("延迟 -");
    QString tokens_text_ = QStringLiteral("已收集词汇：-");
    QString sentence_text_ = QStringLiteral("整理句子：-");
    QString composer_text_ = QStringLiteral("整理来源：-");
    int collect_progress_ = 0;
    std::atomic_bool recording_active_{false};
    bool running_ = false;
};

#endif
