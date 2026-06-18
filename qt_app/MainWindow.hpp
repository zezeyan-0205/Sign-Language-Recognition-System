#ifndef SIGN_LANGUAGE_QT_MAIN_WINDOW_HPP
#define SIGN_LANGUAGE_QT_MAIN_WINDOW_HPP

#include <QByteArray>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>
#include <QTimer>

#include <atomic>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <thread>

class DoctorWindow;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTextEdit;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

private slots:
    void startWorker();
    void stopWorker();
    void startRecording();
    void stopRecording();
    void clearExpression();
    void readStdout();
    void readStderr();
    void updateLocalPreview();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);

private:
    void buildUi();
    void appendLog(const QString& line);
    void consumeStdoutLine(const QByteArray& line);
    void consumeJson(const QJsonObject& object);
    QStringList jsonTokens(const QJsonArray& tokens) const;
    void consumePreview(const QJsonObject& object);
    void startLocalPreview();
    void stopLocalPreview();
    void capturePreviewFrames(int cameraId, int width, int height);
    void showPreviewFrame(const cv::Mat& frame);
    void sendFrameToWorker(const cv::Mat& frame);
    void sendWorkerCommand(const QString& command);
    QString projectPath(const QString& relativePath) const;
    QStringList workerArguments() const;
    void setRunning(bool running);

    QProcess worker_;
    QTimer preview_timer_;
    QElapsedTimer frame_send_timer_;
    std::thread preview_thread_;
    std::mutex preview_mutex_;
    cv::Mat latest_preview_frame_;
    int latest_preview_frame_id_ = 0;
    int shown_preview_frame_id_ = 0;
    std::atomic_bool preview_stop_{false};
    bool local_preview_active_ = false;
    QByteArray stdout_buffer_;
    QByteArray stderr_buffer_;
    DoctorWindow* doctor_window_ = nullptr;

    QLabel* preview_label_ = nullptr;
    QLabel* result_label_ = nullptr;
    QLabel* confidence_label_ = nullptr;
    QLabel* fps_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* latency_label_ = nullptr;
    QLabel* tokens_label_ = nullptr;
    QLabel* sentence_label_ = nullptr;
    QProgressBar* collect_progress_ = nullptr;

    QLineEdit* python_edit_ = nullptr;
    QLineEdit* model_edit_ = nullptr;
    QLineEdit* labels_edit_ = nullptr;
    QLineEdit* camera_edit_ = nullptr;
    QLineEdit* width_edit_ = nullptr;
    QLineEdit* height_edit_ = nullptr;
    QLineEdit* pose_interval_edit_ = nullptr;
    QLineEdit* hand_interval_edit_ = nullptr;
    QLineEdit* json_interval_edit_ = nullptr;

    QPushButton* start_button_ = nullptr;
    QPushButton* stop_button_ = nullptr;
    QPushButton* record_start_button_ = nullptr;
    QPushButton* record_stop_button_ = nullptr;
    QPushButton* clear_button_ = nullptr;
    QTextEdit* log_edit_ = nullptr;
};

#endif
