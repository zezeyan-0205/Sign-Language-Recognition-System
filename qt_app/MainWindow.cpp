#include "MainWindow.hpp"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

QString appProjectRoot()
{
    return QStringLiteral(PROJECT_ROOT);
}

QString jsonNumber(const QJsonObject& object, const QString& key, const QString& suffix = {})
{
    if (!object.contains(key) || !object.value(key).isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(object.value(key).toDouble(), 'f', 2) + suffix;
}

QString latencySummary(const QJsonObject& latency)
{
    if (latency.isEmpty()) {
        return QStringLiteral("latency: -");
    }

    return QStringLiteral("capture %1 ms | pose %2 ms | hand %3 ms | rknn %4 ms | age %5 ms")
        .arg(jsonNumber(latency, QStringLiteral("capture")))
        .arg(jsonNumber(latency, QStringLiteral("pose")))
        .arg(jsonNumber(latency, QStringLiteral("hand")))
        .arg(jsonNumber(latency, QStringLiteral("rknn")))
        .arg(jsonNumber(latency, QStringLiteral("frame_age")));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    buildUi();
    setRunning(false);

    connect(&worker_, &QProcess::readyReadStandardOutput, this, &MainWindow::readStdout);
    connect(&worker_, &QProcess::readyReadStandardError, this, &MainWindow::readStderr);
    connect(&worker_,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            &MainWindow::handleFinished);
    connect(&worker_, &QProcess::errorOccurred, this, &MainWindow::handleError);
}

MainWindow::~MainWindow()
{
    stopWorker();
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("RK3588 手语识别 Qt Demo"));

    auto* root = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root);

    result_label_ = new QLabel(QStringLiteral("等待中..."), root);
    result_label_->setAlignment(Qt::AlignCenter);
    result_label_->setMinimumHeight(170);
    result_label_->setStyleSheet(
        QStringLiteral("font-size: 72px; font-weight: 700; color: #0f8f3a; background: #f7faf7; border: 1px solid #cfd8cf;"));

    auto* metric_row = new QHBoxLayout();
    confidence_label_ = new QLabel(QStringLiteral("confidence: -"), root);
    fps_label_ = new QLabel(QStringLiteral("fps: -"), root);
    status_label_ = new QLabel(QStringLiteral("stopped"), root);
    latency_label_ = new QLabel(QStringLiteral("latency: -"), root);

    for (QLabel* label : {confidence_label_, fps_label_, status_label_}) {
        label->setMinimumHeight(42);
        label->setAlignment(Qt::AlignCenter);
        label->setStyleSheet(QStringLiteral("font-size: 18px; border: 1px solid #d5d9df; background: white;"));
        metric_row->addWidget(label);
    }

    latency_label_->setMinimumHeight(36);
    latency_label_->setAlignment(Qt::AlignCenter);
    latency_label_->setStyleSheet(QStringLiteral("font-size: 15px; color: #3f4650;"));

    auto* config_box = new QGroupBox(QStringLiteral("Python RKNN Worker"), root);
    auto* form = new QFormLayout(config_box);
    python_edit_ = new QLineEdit(QStringLiteral("python3"), config_box);
    model_edit_ = new QLineEdit(QStringLiteral("artifacts/models/sign_tcn_best.rknn"), config_box);
    labels_edit_ = new QLineEdit(QStringLiteral("label_map.json"), config_box);
    camera_edit_ = new QLineEdit(QStringLiteral("21"), config_box);
    width_edit_ = new QLineEdit(QStringLiteral("1280"), config_box);
    height_edit_ = new QLineEdit(QStringLiteral("720"), config_box);
    pose_interval_edit_ = new QLineEdit(QStringLiteral("8"), config_box);
    hand_interval_edit_ = new QLineEdit(QStringLiteral("4"), config_box);
    json_interval_edit_ = new QLineEdit(QStringLiteral("0.2"), config_box);

    form->addRow(QStringLiteral("Python"), python_edit_);
    form->addRow(QStringLiteral("Model"), model_edit_);
    form->addRow(QStringLiteral("Labels"), labels_edit_);
    form->addRow(QStringLiteral("Camera"), camera_edit_);
    form->addRow(QStringLiteral("Width"), width_edit_);
    form->addRow(QStringLiteral("Height"), height_edit_);
    form->addRow(QStringLiteral("Pose interval"), pose_interval_edit_);
    form->addRow(QStringLiteral("Hand interval"), hand_interval_edit_);
    form->addRow(QStringLiteral("JSON interval"), json_interval_edit_);

    start_button_ = new QPushButton(QStringLiteral("启动识别"), root);
    stop_button_ = new QPushButton(QStringLiteral("停止"), root);
    connect(start_button_, &QPushButton::clicked, this, &MainWindow::startWorker);
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopWorker);

    auto* button_row = new QHBoxLayout();
    button_row->addWidget(start_button_);
    button_row->addWidget(stop_button_);

    log_edit_ = new QTextEdit(root);
    log_edit_->setReadOnly(true);
    log_edit_->setMinimumHeight(160);

    root_layout->addWidget(result_label_);
    root_layout->addLayout(metric_row);
    root_layout->addWidget(latency_label_);
    root_layout->addWidget(config_box);
    root_layout->addLayout(button_row);
    root_layout->addWidget(log_edit_, 1);
    setCentralWidget(root);
}

void MainWindow::startWorker()
{
    if (worker_.state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("worker already running"));
        return;
    }

    stdout_buffer_.clear();
    stderr_buffer_.clear();
    result_label_->setText(QStringLiteral("启动中..."));
    confidence_label_->setText(QStringLiteral("confidence: -"));
    fps_label_->setText(QStringLiteral("fps: -"));
    latency_label_->setText(QStringLiteral("latency: -"));

    worker_.setWorkingDirectory(appProjectRoot());
    worker_.setProcessChannelMode(QProcess::SeparateChannels);

    const QString program = python_edit_->text().trimmed();
    const QStringList arguments = workerArguments();
    appendLog(QStringLiteral("$ cd %1").arg(appProjectRoot()));
    appendLog(QStringLiteral("$ %1 %2").arg(program, arguments.join(QStringLiteral(" "))));

    worker_.start(program, arguments);
    setRunning(true);
}

void MainWindow::stopWorker()
{
    if (worker_.state() == QProcess::NotRunning) {
        return;
    }

    worker_.terminate();
    if (!worker_.waitForFinished(1500)) {
        worker_.kill();
        worker_.waitForFinished(1500);
    }
}

void MainWindow::readStdout()
{
    stdout_buffer_.append(worker_.readAllStandardOutput());
    int newline = -1;
    while ((newline = stdout_buffer_.indexOf('\n')) >= 0) {
        const QByteArray line = stdout_buffer_.left(newline).trimmed();
        stdout_buffer_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            consumeStdoutLine(line);
        }
    }
}

void MainWindow::readStderr()
{
    stderr_buffer_.append(worker_.readAllStandardError());
    int newline = -1;
    while ((newline = stderr_buffer_.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(stderr_buffer_.left(newline).trimmed());
        stderr_buffer_.remove(0, newline + 1);
        if (!line.isEmpty()) {
            appendLog(QStringLiteral("[stderr] %1").arg(line));
        }
    }
}

void MainWindow::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    setRunning(false);
    status_label_->setText(QStringLiteral("stopped"));
    appendLog(QStringLiteral("worker finished, exitCode=%1").arg(exitCode));
}

void MainWindow::handleError(QProcess::ProcessError error)
{
    appendLog(QStringLiteral("worker error: %1").arg(static_cast<int>(error)));
    status_label_->setText(QStringLiteral("error"));
    setRunning(false);
}

void MainWindow::appendLog(const QString& line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    log_edit_->append(QStringLiteral("[%1] %2").arg(stamp, line));
}

void MainWindow::consumeStdoutLine(const QByteArray& line)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        consumeJson(doc.object());
        return;
    }

    appendLog(QString::fromUtf8(line));
}

void MainWindow::consumeJson(const QJsonObject& object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready")) {
        status_label_->setText(QStringLiteral("ready"));
        appendLog(QStringLiteral("worker ready"));
        return;
    }

    if (type == QStringLiteral("stopped")) {
        status_label_->setText(QStringLiteral("stopped"));
        appendLog(QStringLiteral("worker stopped"));
        return;
    }

    if (type != QStringLiteral("result")) {
        appendLog(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        return;
    }

    const QString text = object.value(QStringLiteral("text")).toString(QStringLiteral("识别中..."));
    result_label_->setText(text);
    confidence_label_->setText(QStringLiteral("confidence: %1").arg(jsonNumber(object, QStringLiteral("confidence"))));
    fps_label_->setText(QStringLiteral("fps: %1").arg(jsonNumber(object, QStringLiteral("fps"))));

    const bool hands_valid = object.value(QStringLiteral("hands_valid")).toBool(false);
    const int sequence_frames = object.value(QStringLiteral("sequence_frames")).toInt(0);
    const double window_sec = object.value(QStringLiteral("window_sec")).toDouble(0.0);
    status_label_->setText(
        QStringLiteral("%1 | frames %2 | window %3s")
            .arg(hands_valid ? QStringLiteral("tracking") : QStringLiteral("waiting"))
            .arg(sequence_frames)
            .arg(window_sec, 0, 'f', 2));

    latency_label_->setText(latencySummary(object.value(QStringLiteral("latency_ms")).toObject()));
}

QString MainWindow::projectPath(const QString& relativePath) const
{
    if (relativePath.startsWith('/')) {
        return relativePath;
    }
    return appProjectRoot() + QStringLiteral("/") + relativePath;
}

QStringList MainWindow::workerArguments() const
{
    return {
        projectPath(QStringLiteral("scripts/infer/infer_realtime_rknn.py")),
        QStringLiteral("--model"), model_edit_->text().trimmed(),
        QStringLiteral("--labels"), labels_edit_->text().trimmed(),
        QStringLiteral("--camera"), camera_edit_->text().trimmed(),
        QStringLiteral("--width"), width_edit_->text().trimmed(),
        QStringLiteral("--height"), height_edit_->text().trimmed(),
        QStringLiteral("--pose-interval"), pose_interval_edit_->text().trimmed(),
        QStringLiteral("--hand-interval"), hand_interval_edit_->text().trimmed(),
        QStringLiteral("--json-interval"), json_interval_edit_->text().trimmed(),
        QStringLiteral("--no-window"),
    };
}

void MainWindow::setRunning(bool running)
{
    start_button_->setEnabled(!running);
    stop_button_->setEnabled(running);
    status_label_->setText(running ? QStringLiteral("starting") : status_label_->text());
}
