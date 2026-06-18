#ifndef SIGN_LANGUAGE_QT_MAIN_WINDOW_HPP
#define SIGN_LANGUAGE_QT_MAIN_WINDOW_HPP

#include <QByteArray>
#include <QJsonObject>
#include <QMainWindow>
#include <QProcess>

class QLabel;
class QLineEdit;
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
    void readStdout();
    void readStderr();
    void handleFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void handleError(QProcess::ProcessError error);

private:
    void buildUi();
    void appendLog(const QString& line);
    void consumeStdoutLine(const QByteArray& line);
    void consumeJson(const QJsonObject& object);
    QString projectPath(const QString& relativePath) const;
    QStringList workerArguments() const;
    void setRunning(bool running);

    QProcess worker_;
    QByteArray stdout_buffer_;
    QByteArray stderr_buffer_;

    QLabel* result_label_ = nullptr;
    QLabel* confidence_label_ = nullptr;
    QLabel* fps_label_ = nullptr;
    QLabel* status_label_ = nullptr;
    QLabel* latency_label_ = nullptr;

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
    QTextEdit* log_edit_ = nullptr;
};

#endif
