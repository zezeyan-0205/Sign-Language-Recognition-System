#include "SignController.hpp"

#include "PreviewImageProvider.hpp"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QUrlQuery>

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

QString jsonNumber(const QJsonObject& object, const QString& key)
{
    if (!object.contains(key) || !object.value(key).isDouble()) {
        return QStringLiteral("-");
    }
    return QString::number(object.value(key).toDouble(), 'f', 2);
}

QString latencySummary(const QJsonObject& latency)
{
    if (latency.isEmpty()) {
        return QStringLiteral("延迟 -");
    }

    return QStringLiteral("采集 %1 ms | 姿态 %2 ms | 手部 %3 ms | NPU %4 ms | 帧龄 %5 ms")
        .arg(jsonNumber(latency, QStringLiteral("capture")))
        .arg(jsonNumber(latency, QStringLiteral("pose")))
        .arg(jsonNumber(latency, QStringLiteral("hand")))
        .arg(jsonNumber(latency, QStringLiteral("rknn")))
        .arg(jsonNumber(latency, QStringLiteral("frame_age")));
}

}  // namespace

SignController::SignController(PreviewImageProvider* previewProvider, QObject* parent)
    : QObject(parent)
    , preview_provider_(previewProvider)
{
    connect(&worker_, &QProcess::readyReadStandardOutput, this, &SignController::readStdout);
    connect(&worker_, &QProcess::readyReadStandardError, this, &SignController::readStderr);
    connect(&preview_timer_, &QTimer::timeout, this, &SignController::updateLocalPreview);
    connect(&worker_,
            static_cast<void (QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this,
            &SignController::handleFinished);
    connect(&worker_, &QProcess::errorOccurred, this, &SignController::handleError);
}

SignController::~SignController()
{
    stopSystem();
    stopLocalPreview();
}

void SignController::startSystem()
{
    if (worker_.state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("worker already running"));
        return;
    }

    stdout_buffer_.clear();
    stderr_buffer_.clear();
    current_word_ = QStringLiteral("启动中...");
    confidence_text_ = QStringLiteral("置信度 -");
    fps_text_ = QStringLiteral("FPS -");
    latency_text_ = QStringLiteral("延迟 -");
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：-");
    composer_text_ = QStringLiteral("整理来源：-");
    setProgress(0);
    emit currentWordChanged();
    emit metricsChanged();
    emit tokensChanged();
    emit sentenceChanged();

    worker_.setWorkingDirectory(appProjectRoot());
    worker_.setProcessChannelMode(QProcess::SeparateChannels);

    const QString program = QStringLiteral("python3");
    const QStringList arguments = workerArguments();
    appendLog(QStringLiteral("$ cd %1").arg(appProjectRoot()));
    appendLog(QStringLiteral("$ %1 %2").arg(program, arguments.join(QStringLiteral(" "))));

    worker_.start(program, arguments);
    startLocalPreview();
    setRunning(true);
    setStatus(QStringLiteral("启动中"));
}

void SignController::stopSystem()
{
    if (worker_.state() != QProcess::NotRunning) {
        worker_.terminate();
        if (!worker_.waitForFinished(1500)) {
            worker_.kill();
            worker_.waitForFinished(1500);
        }
    }
    stopLocalPreview();
    setRunning(false);
}

void SignController::startRecording()
{
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：表达中...");
    composer_text_ = QStringLiteral("整理来源：-");
    setProgress(0);
    emit tokensChanged();
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("start_recording"));
}

void SignController::stopRecording()
{
    sentence_text_ = QStringLiteral("整理句子：正在整理...");
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("stop_recording"));
}

void SignController::clearExpression()
{
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：-");
    composer_text_ = QStringLiteral("整理来源：-");
    setProgress(0);
    emit tokensChanged();
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("clear"));
}

void SignController::readStdout()
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

void SignController::readStderr()
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

void SignController::updateLocalPreview()
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
    publishPreviewFrame(frame);
    sendFrameToWorker(frame);
}

void SignController::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    stopLocalPreview();
    setRunning(false);
    setStatus(QStringLiteral("已停止"));
    appendLog(QStringLiteral("worker finished, exitCode=%1").arg(exitCode));
}

void SignController::handleError(QProcess::ProcessError error)
{
    stopLocalPreview();
    setRunning(false);
    setStatus(QStringLiteral("错误"));
    appendLog(QStringLiteral("worker error: %1").arg(static_cast<int>(error)));
}

void SignController::appendLog(const QString& line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString message = QStringLiteral("[%1] %2").arg(stamp, line);
    std::cout << message.toStdString() << std::endl;
    emit logLine(message);
}

void SignController::consumeStdoutLine(const QByteArray& line)
{
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error == QJsonParseError::NoError && doc.isObject()) {
        consumeJson(doc.object());
        return;
    }

    appendLog(QString::fromUtf8(line));
}

void SignController::consumeJson(const QJsonObject& object)
{
    const QString type = object.value(QStringLiteral("type")).toString();
    if (type == QStringLiteral("ready")) {
        setStatus(QStringLiteral("就绪"));
        appendLog(QStringLiteral("worker ready"));
        return;
    }

    if (type == QStringLiteral("stopped")) {
        setStatus(QStringLiteral("已停止"));
        appendLog(QStringLiteral("worker stopped"));
        return;
    }

    if (type == QStringLiteral("recording")) {
        const bool active = object.value(QStringLiteral("active")).toBool(false);
        setStatus(active ? QStringLiteral("表达采集中") : QStringLiteral("就绪"));
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
        return;
    }

    if (type == QStringLiteral("tokens")) {
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
        return;
    }

    if (type == QStringLiteral("sentence")) {
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        const QString sentence = object.value(QStringLiteral("sentence")).toString();
        const QString composer = object.value(QStringLiteral("composer")).toString();
        sentence_text_ = sentence.isEmpty() ? QStringLiteral("整理句子：-") : QStringLiteral("整理句子：%1").arg(sentence);
        composer_text_ = composer.isEmpty() ? QStringLiteral("整理来源：-") : QStringLiteral("整理来源：%1").arg(composer);
        emit tokensChanged();
        emit sentenceChanged();
        if (!composer.isEmpty()) {
            appendLog(QStringLiteral("sentence composer: %1").arg(composer));
        }
        return;
    }

    if (type != QStringLiteral("result")) {
        appendLog(QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact)));
        return;
    }

    current_word_ = object.value(QStringLiteral("text")).toString(QStringLiteral("识别中..."));
    confidence_text_ = QStringLiteral("置信度 %1").arg(jsonNumber(object, QStringLiteral("confidence")));
    fps_text_ = QStringLiteral("FPS %1").arg(jsonNumber(object, QStringLiteral("fps")));
    latency_text_ = latencySummary(object.value(QStringLiteral("latency_ms")).toObject());
    emit currentWordChanged();
    emit metricsChanged();

    if (object.contains(QStringLiteral("tokens"))) {
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
    }

    const QString sentence = object.value(QStringLiteral("sentence")).toString();
    if (!sentence.isEmpty()) {
        sentence_text_ = QStringLiteral("整理句子：%1").arg(sentence);
        emit sentenceChanged();
    }

    const bool hands_valid = object.value(QStringLiteral("hands_valid")).toBool(false);
    const int sequence_frames = object.value(QStringLiteral("sequence_frames")).toInt(0);
    const double window_sec = object.value(QStringLiteral("window_sec")).toDouble(0.0);
    setProgress(qBound(0, static_cast<int>((window_sec / 1.5) * 100.0), 100));
    setStatus(
        QStringLiteral("%1 | frames %2 | window %3s")
            .arg(hands_valid ? QStringLiteral("追踪中") : QStringLiteral("等待手部"))
            .arg(sequence_frames)
            .arg(window_sec, 0, 'f', 2));
}

QStringList SignController::jsonTokens(const QJsonArray& tokens) const
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

QString SignController::tokensDisplayText(const QJsonArray& tokens) const
{
    const QStringList values = jsonTokens(tokens);
    return values.isEmpty() ? QStringLiteral("已收集词汇：-") : QStringLiteral("已收集词汇：%1").arg(values.join(QStringLiteral(" / ")));
}

void SignController::startLocalPreview()
{
    stopLocalPreview();

    const int camera_id = 21;
    const int width = 1280;
    const int height = 720;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        latest_preview_frame_.release();
        latest_preview_frame_id_ = 0;
        shown_preview_frame_id_ = 0;
    }
    preview_stop_.store(false);
    local_preview_active_ = true;
    preview_thread_ = std::thread(&SignController::capturePreviewFrames, this, camera_id, width, height);
    preview_timer_.start(33);
    appendLog(QStringLiteral("Qt QML local camera preview thread started: /dev/video%1").arg(camera_id));
}

void SignController::stopLocalPreview()
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

void SignController::capturePreviewFrames(int cameraId, int width, int height)
{
    const QString device_path = QStringLiteral("/dev/video%1").arg(cameraId);
    cv::VideoCapture cap(device_path.toStdString(), cv::CAP_V4L2);
    if (!cap.isOpened()) {
        QMetaObject::invokeMethod(
            this,
            [this, device_path]() {
                appendLog(QStringLiteral("Qt QML camera preview open failed: %1").arg(device_path));
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

void SignController::publishPreviewFrame(const cv::Mat& frame)
{
    if (!preview_provider_) {
        return;
    }

    cv::Mat display = frame.clone();
    const int x1 = static_cast<int>(display.cols * 0.15);
    const int x2 = static_cast<int>(display.cols * 0.85);
    cv::rectangle(display, cv::Point(x1, 0), cv::Point(x2, display.rows - 1), cv::Scalar(255, 255, 0), 3);

    cv::Mat rgb;
    cv::cvtColor(display, rgb, cv::COLOR_BGR2RGB);
    QImage image(rgb.data, rgb.cols, rgb.rows, static_cast<int>(rgb.step), QImage::Format_RGB888);
    preview_provider_->setImage(image);

    QUrl url(QStringLiteral("image://signpreview/live"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("frame"), QString::number(++published_preview_frame_id_));
    url.setQuery(query);
    preview_source_ = url;
    emit previewSourceChanged();
}

void SignController::sendFrameToWorker(const cv::Mat& frame)
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

void SignController::sendWorkerCommand(const QString& command)
{
    if (worker_.state() == QProcess::NotRunning) {
        appendLog(QStringLiteral("worker is not running"));
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("cmd"), command);
    worker_.write(QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n');
    appendLog(QStringLiteral("command: %1").arg(command));
}

QString SignController::projectPath(const QString& relativePath) const
{
    if (relativePath.startsWith('/')) {
        return relativePath;
    }
    return appProjectRoot() + QStringLiteral("/") + relativePath;
}

QStringList SignController::workerArguments() const
{
    return {
        projectPath(QStringLiteral("scripts/infer/infer_realtime_rknn.py")),
        QStringLiteral("--model"), QStringLiteral("artifacts/models/sign_tcn_best.rknn"),
        QStringLiteral("--labels"), QStringLiteral("label_map.json"),
        QStringLiteral("--camera"), QStringLiteral("21"),
        QStringLiteral("--width"), QStringLiteral("1280"),
        QStringLiteral("--height"), QStringLiteral("720"),
        QStringLiteral("--pose-interval"), QStringLiteral("4"),
        QStringLiteral("--hand-interval"), QStringLiteral("2"),
        QStringLiteral("--json-interval"), QStringLiteral("0.2"),
        QStringLiteral("--frame-stdin"),
        QStringLiteral("--composer"), QStringLiteral("rkllm"),
        QStringLiteral("--rkllm-url"), QStringLiteral("http://127.0.0.1:8080/rkllm_chat"),
        QStringLiteral("--llm-timeout"), QStringLiteral("8"),
        QStringLiteral("--no-window"),
    };
}

void SignController::setRunning(bool running)
{
    if (running_ == running) {
        return;
    }
    running_ = running;
    emit runningChanged();
}

void SignController::setStatus(const QString& status)
{
    if (status_text_ == status) {
        return;
    }
    status_text_ = status;
    emit statusChanged();
}

void SignController::setProgress(int progress)
{
    progress = qBound(0, progress, 100);
    if (collect_progress_ == progress) {
        return;
    }
    collect_progress_ = progress;
    emit progressChanged();
}
