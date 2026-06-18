#include "MainWindow.hpp"

#include "DoctorWindow.hpp"

#include <QDateTime>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPixmap>
#include <QProgressBar>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>

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

QString tokensText(const QJsonArray& tokens)
{
    QStringList values;
    values.reserve(tokens.size());
    for (const QJsonValue& value : tokens) {
        const QString token = value.toString().trimmed();
        if (!token.isEmpty()) {
            values.push_back(token);
        }
    }
    return values.isEmpty() ? QStringLiteral("已收集词汇：-") : QStringLiteral("已收集词汇：%1").arg(values.join(QStringLiteral(" / ")));
}

}  // namespace

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
{
    doctor_window_ = new DoctorWindow();
    doctor_window_->resize(900, 760);
    doctor_window_->show();

    buildUi();
    setRunning(false);

    connect(&worker_, &QProcess::readyReadStandardOutput, this, &MainWindow::readStdout);
    connect(&worker_, &QProcess::readyReadStandardError, this, &MainWindow::readStderr);
    connect(&preview_timer_, &QTimer::timeout, this, &MainWindow::updateLocalPreview);
    connect(&worker_,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            &MainWindow::handleFinished);
    connect(&worker_, &QProcess::errorOccurred, this, &MainWindow::handleError);
}

MainWindow::~MainWindow()
{
    stopWorker();
    stopLocalPreview();
    if (doctor_window_) {
        doctor_window_->close();
        delete doctor_window_;
        doctor_window_ = nullptr;
    }
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("医院手语交互系统"));

    auto* root = new QWidget(this);
    auto* root_layout = new QVBoxLayout(root);
    root_layout->setSpacing(14);

    preview_label_ = new QLabel(QStringLiteral("摄像头画面等待中..."), root);
    preview_label_->setAlignment(Qt::AlignCenter);
    preview_label_->setMinimumSize(560, 420);
    preview_label_->setStyleSheet(QStringLiteral("font-size: 28px; color: #5b6470; background: #111820; border: 1px solid #27313c;"));

    result_label_ = new QLabel(QStringLiteral("等待中..."), root);
    result_label_->setAlignment(Qt::AlignCenter);
    result_label_->setMinimumHeight(180);
    result_label_->setStyleSheet(
        QStringLiteral("font-size: 78px; font-weight: 700; color: #0f8f3a; background: #f7faf7; border: 1px solid #cfd8cf;"));

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

    tokens_label_ = new QLabel(QStringLiteral("已收集词汇：-"), root);
    tokens_label_->setMinimumHeight(46);
    tokens_label_->setAlignment(Qt::AlignCenter);
    tokens_label_->setStyleSheet(QStringLiteral("font-size: 20px; color: #2f3846; background: #fffdf5; border: 1px solid #e0d7aa;"));

    sentence_label_ = new QLabel(QStringLiteral("整理句子：-"), root);
    sentence_label_->setMinimumHeight(100);
    sentence_label_->setAlignment(Qt::AlignCenter);
    sentence_label_->setWordWrap(true);
    sentence_label_->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: 600; color: #1d4b73; background: #f4f9ff; border: 1px solid #bed0df;"));

    collect_progress_ = new QProgressBar(root);
    collect_progress_->setRange(0, 100);
    collect_progress_->setValue(0);
    collect_progress_->setMinimumHeight(34);
    collect_progress_->setFormat(QStringLiteral("采集进度 %p%"));
    collect_progress_->setStyleSheet(QStringLiteral("QProgressBar { font-size: 18px; text-align: center; border: 1px solid #b7c3d0; background: #f2f5f8; } QProgressBar::chunk { background: #1f9d55; }"));

    auto* config_box = new QGroupBox(QStringLiteral("运行设置"), root);
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

    form->addRow(QStringLiteral("摄像头编号"), camera_edit_);

    start_button_ = new QPushButton(QStringLiteral("启动系统"), root);
    stop_button_ = new QPushButton(QStringLiteral("停止系统"), root);
    record_start_button_ = new QPushButton(QStringLiteral("开始表达"), root);
    record_stop_button_ = new QPushButton(QStringLiteral("结束表达"), root);
    clear_button_ = new QPushButton(QStringLiteral("清空表达"), root);
    for (QPushButton* button : {start_button_, stop_button_, record_start_button_, record_stop_button_, clear_button_}) {
        button->setMinimumHeight(72);
        button->setStyleSheet(QStringLiteral("font-size: 26px; font-weight: 600;"));
    }
    connect(start_button_, &QPushButton::clicked, this, &MainWindow::startWorker);
    connect(stop_button_, &QPushButton::clicked, this, &MainWindow::stopWorker);
    connect(record_start_button_, &QPushButton::clicked, this, &MainWindow::startRecording);
    connect(record_stop_button_, &QPushButton::clicked, this, &MainWindow::stopRecording);
    connect(clear_button_, &QPushButton::clicked, this, &MainWindow::clearExpression);

    auto* button_row = new QHBoxLayout();
    button_row->addWidget(record_start_button_);
    button_row->addWidget(record_stop_button_);
    button_row->addWidget(clear_button_);

    auto* system_button_row = new QHBoxLayout();
    system_button_row->addWidget(start_button_);
    system_button_row->addWidget(stop_button_);

    log_edit_ = new QTextEdit(root);
    log_edit_->setReadOnly(true);
    log_edit_->setMinimumHeight(110);

    auto* patient_panel = new QVBoxLayout();
    patient_panel->addWidget(result_label_);
    patient_panel->addLayout(metric_row);
    patient_panel->addWidget(latency_label_);
    patient_panel->addWidget(tokens_label_);
    patient_panel->addWidget(sentence_label_);
    patient_panel->addWidget(collect_progress_);
    patient_panel->addLayout(button_row);

    auto* main_row = new QHBoxLayout();
    main_row->addWidget(preview_label_, 1);
    main_row->addLayout(patient_panel, 1);

    root_layout->addLayout(main_row, 1);
    auto* bottom_row = new QHBoxLayout();
    bottom_row->addWidget(config_box);
    bottom_row->addLayout(system_button_row);
    root_layout->addLayout(bottom_row);
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
    tokens_label_->setText(QStringLiteral("已收集词汇：-"));
    sentence_label_->setText(QStringLiteral("整理句子：-"));
    collect_progress_->setValue(0);
    if (doctor_window_) {
        doctor_window_->clearRecord();
    }

    worker_.setWorkingDirectory(appProjectRoot());
    worker_.setProcessChannelMode(QProcess::SeparateChannels);

    const QString program = python_edit_->text().trimmed();
    const QStringList arguments = workerArguments();
    appendLog(QStringLiteral("$ cd %1").arg(appProjectRoot()));
    appendLog(QStringLiteral("$ %1 %2").arg(program, arguments.join(QStringLiteral(" "))));

    worker_.start(program, arguments);
    startLocalPreview();
    setRunning(true);
}

void MainWindow::startRecording()
{
    tokens_label_->setText(QStringLiteral("已收集词汇：-"));
    sentence_label_->setText(QStringLiteral("整理句子：表达中..."));
    collect_progress_->setValue(0);
    sendWorkerCommand(QStringLiteral("start_recording"));
}

void MainWindow::stopRecording()
{
    sentence_label_->setText(QStringLiteral("整理句子：正在整理..."));
    sendWorkerCommand(QStringLiteral("stop_recording"));
}

void MainWindow::clearExpression()
{
    tokens_label_->setText(QStringLiteral("已收集词汇：-"));
    sentence_label_->setText(QStringLiteral("整理句子：-"));
    collect_progress_->setValue(0);
    sendWorkerCommand(QStringLiteral("clear"));
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
    stopLocalPreview();
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

void MainWindow::updateLocalPreview()
{
    if (!local_preview_active_) {
        return;
    }

    cv::Mat frame;
    int frame_id = 0;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        if (latest_preview_frame_.empty() || latest_preview_frame_id_ == shown_preview_frame_id_) {
            return;
        }
        frame = latest_preview_frame_.clone();
        frame_id = latest_preview_frame_id_;
    }
    if (frame.empty()) {
        return;
    }

    shown_preview_frame_id_ = frame_id;
    showPreviewFrame(frame);
    sendFrameToWorker(frame);
}

void MainWindow::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    stopLocalPreview();
    setRunning(false);
    status_label_->setText(QStringLiteral("stopped"));
    appendLog(QStringLiteral("worker finished, exitCode=%1").arg(exitCode));
}

void MainWindow::handleError(QProcess::ProcessError error)
{
    stopLocalPreview();
    appendLog(QStringLiteral("worker error: %1").arg(static_cast<int>(error)));
    status_label_->setText(QStringLiteral("error"));
    setRunning(false);
}

void MainWindow::appendLog(const QString& line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString message = QStringLiteral("[%1] %2").arg(stamp, line);
    log_edit_->append(message);
    std::cout << message.toStdString() << std::endl;
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

    if (type == QStringLiteral("preview")) {
        consumePreview(object);
        return;
    }

    if (type == QStringLiteral("recording")) {
        const bool active = object.value(QStringLiteral("active")).toBool(false);
        status_label_->setText(active ? QStringLiteral("recording") : QStringLiteral("ready"));
        tokens_label_->setText(tokensText(object.value(QStringLiteral("tokens")).toArray()));
        if (doctor_window_) {
            doctor_window_->updateTokens(jsonTokens(object.value(QStringLiteral("tokens")).toArray()));
        }
        return;
    }

    if (type == QStringLiteral("tokens")) {
        tokens_label_->setText(tokensText(object.value(QStringLiteral("tokens")).toArray()));
        if (doctor_window_) {
            doctor_window_->updateTokens(jsonTokens(object.value(QStringLiteral("tokens")).toArray()));
        }
        return;
    }

    if (type == QStringLiteral("sentence")) {
        const QStringList tokens = jsonTokens(object.value(QStringLiteral("tokens")).toArray());
        tokens_label_->setText(tokensText(object.value(QStringLiteral("tokens")).toArray()));
        const QString sentence = object.value(QStringLiteral("sentence")).toString();
        const QString composer = object.value(QStringLiteral("composer")).toString();
        sentence_label_->setText(sentence.isEmpty() ? QStringLiteral("整理句子：-") : QStringLiteral("整理句子：%1").arg(sentence));
        if (!composer.isEmpty()) {
            appendLog(QStringLiteral("sentence composer: %1").arg(composer));
        }
        if (doctor_window_) {
            doctor_window_->updateTokens(tokens);
            doctor_window_->updateSentence(sentence);
        }
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
    if (object.contains(QStringLiteral("tokens"))) {
        const QStringList tokens = jsonTokens(object.value(QStringLiteral("tokens")).toArray());
        tokens_label_->setText(tokensText(object.value(QStringLiteral("tokens")).toArray()));
        if (doctor_window_) {
            doctor_window_->updateTokens(tokens);
        }
    }
    const QString sentence = object.value(QStringLiteral("sentence")).toString();
    if (!sentence.isEmpty()) {
        sentence_label_->setText(QStringLiteral("整理句子：%1").arg(sentence));
        if (doctor_window_) {
            doctor_window_->updateSentence(sentence);
        }
    }
    if (doctor_window_) {
        doctor_window_->updateLiveWord(text, object.value(QStringLiteral("confidence")).toDouble(0.0));
    }

    const bool hands_valid = object.value(QStringLiteral("hands_valid")).toBool(false);
    const int sequence_frames = object.value(QStringLiteral("sequence_frames")).toInt(0);
    const double window_sec = object.value(QStringLiteral("window_sec")).toDouble(0.0);
    collect_progress_->setValue(qBound(0, static_cast<int>((window_sec / 1.5) * 100.0), 100));
    status_label_->setText(
        QStringLiteral("%1 | frames %2 | window %3s")
            .arg(hands_valid ? QStringLiteral("tracking") : QStringLiteral("waiting"))
            .arg(sequence_frames)
            .arg(window_sec, 0, 'f', 2));

    latency_label_->setText(latencySummary(object.value(QStringLiteral("latency_ms")).toObject()));
}

QStringList MainWindow::jsonTokens(const QJsonArray& tokens) const
{
    QStringList values;
    values.reserve(tokens.size());
    for (const QJsonValue& value : tokens) {
        const QString token = value.toString().trimmed();
        if (!token.isEmpty()) {
            values.push_back(token);
        }
    }
    return values;
}

void MainWindow::consumePreview(const QJsonObject& object)
{
    if (local_preview_active_) {
        return;
    }

    const QByteArray jpeg = QByteArray::fromBase64(object.value(QStringLiteral("image")).toString().toLatin1());
    QPixmap pixmap;
    if (!pixmap.loadFromData(jpeg, "JPG")) {
        return;
    }

    preview_label_->setPixmap(
        pixmap.scaled(preview_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::startLocalPreview()
{
    stopLocalPreview();

    bool ok = false;
    const int camera_id = camera_edit_->text().trimmed().toInt(&ok);
    if (!ok) {
        appendLog(QStringLiteral("invalid camera id for Qt local preview"));
        return;
    }

    const int width = width_edit_->text().trimmed().toInt();
    const int height = height_edit_->text().trimmed().toInt();
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        latest_preview_frame_.release();
        latest_preview_frame_id_ = 0;
        shown_preview_frame_id_ = 0;
    }
    preview_stop_.store(false);
    local_preview_active_ = true;
    preview_thread_ = std::thread(&MainWindow::capturePreviewFrames, this, camera_id, width, height);
    preview_timer_.start(33);
    appendLog(QStringLiteral("Qt local camera preview thread started: /dev/video%1").arg(camera_id));
}

void MainWindow::stopLocalPreview()
{
    preview_timer_.stop();
    preview_stop_.store(true);
    if (preview_thread_.joinable()) {
        preview_thread_.join();
    }
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        latest_preview_frame_.release();
        latest_preview_frame_id_ = 0;
        shown_preview_frame_id_ = 0;
    }
    local_preview_active_ = false;
}

void MainWindow::capturePreviewFrames(int cameraId, int width, int height)
{
    const QString device_path = QStringLiteral("/dev/video%1").arg(cameraId);
    cv::VideoCapture cap(device_path.toStdString(), cv::CAP_V4L2);
    if (!cap.isOpened()) {
        QMetaObject::invokeMethod(
            this,
            [this, device_path]() {
                appendLog(QStringLiteral("Qt local camera preview open failed: %1").arg(device_path));
                stopLocalPreview();
            },
            Qt::QueuedConnection);
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FPS, 30);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

    while (!preview_stop_.load()) {
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }

        std::lock_guard<std::mutex> lock(preview_mutex_);
        latest_preview_frame_ = frame;
        ++latest_preview_frame_id_;
    }
}

void MainWindow::showPreviewFrame(const cv::Mat& frame)
{
    cv::Mat display = frame.clone();
    const int x1 = static_cast<int>(display.cols * 0.15);
    const int x2 = static_cast<int>(display.cols * 0.85);
    const int y1 = 0;
    const int y2 = display.rows - 1;
    cv::rectangle(display, cv::Point(x1, y1), cv::Point(x2, y2), cv::Scalar(255, 255, 0), 3);

    cv::Mat rgb;
    cv::cvtColor(display, rgb, cv::COLOR_BGR2RGB);
    QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    preview_label_->setPixmap(
        QPixmap::fromImage(image.copy()).scaled(preview_label_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::sendFrameToWorker(const cv::Mat& frame)
{
    if (worker_.state() == QProcess::NotRunning) {
        return;
    }
    if (worker_.bytesToWrite() > 4 * 1024 * 1024) {
        return;
    }
    if (frame_send_timer_.isValid() && frame_send_timer_.elapsed() < 30) {
        return;
    }
    frame_send_timer_.restart();

    cv::Mat send_frame = frame;
    const int target_width = 640;
    if (send_frame.cols > target_width) {
        const int target_height = std::max(1, send_frame.rows * target_width / send_frame.cols);
        cv::Mat resized;
        cv::resize(send_frame, resized, cv::Size(target_width, target_height), 0, 0, cv::INTER_AREA);
        send_frame = resized;
    }

    std::vector<uchar> encoded;
    if (!cv::imencode(".jpg", send_frame, encoded, {cv::IMWRITE_JPEG_QUALITY, 70})) {
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("cmd"), QStringLiteral("frame"));
    object.insert(
        QStringLiteral("image"),
        QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(encoded.data()), static_cast<int>(encoded.size())).toBase64()));
    worker_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
}

void MainWindow::sendWorkerCommand(const QString& command)
{
    if (worker_.state() == QProcess::NotRunning) {
        appendLog(QStringLiteral("worker is not running"));
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("cmd"), command);
    const QByteArray line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
    worker_.write(line);
    appendLog(QStringLiteral("command: %1").arg(command));
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
        QStringLiteral("--pose-interval"), QStringLiteral("4"),
        QStringLiteral("--hand-interval"), QStringLiteral("2"),
        QStringLiteral("--json-interval"), json_interval_edit_->text().trimmed(),
        QStringLiteral("--frame-stdin"),
        QStringLiteral("--composer"), QStringLiteral("rkllm"),
        QStringLiteral("--rkllm-url"), QStringLiteral("http://127.0.0.1:8080/rkllm_chat"),
        QStringLiteral("--llm-timeout"), QStringLiteral("8"),
        QStringLiteral("--no-window"),
    };
}

void MainWindow::setRunning(bool running)
{
    start_button_->setEnabled(!running);
    stop_button_->setEnabled(running);
    record_start_button_->setEnabled(running);
    record_stop_button_->setEnabled(running);
    clear_button_->setEnabled(running);
    status_label_->setText(running ? QStringLiteral("starting") : status_label_->text());
}
