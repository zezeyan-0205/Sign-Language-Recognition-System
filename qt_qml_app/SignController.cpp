#include "SignController.hpp"

#include "CameraView.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QTextStream>
#include <QVariantMap>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>

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

QDir recordsDirectory()
{
    QDir dir(appProjectRoot());
    dir.mkpath(QStringLiteral("artifacts/records"));
    dir.cd(QStringLiteral("artifacts/records"));
    return dir;
}

QString recordTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString displayTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString safeRecordPath(const QString& path)
{
    if (path.isEmpty()) {
        return QString();
    }

    const QDir dir = recordsDirectory();
    const QString canonicalDir = dir.canonicalPath();
    const QFileInfo fileInfo(path);
    const QString absolutePath = fileInfo.isAbsolute()
        ? fileInfo.absoluteFilePath()
        : dir.absoluteFilePath(path);
    const QString cleanPath = QDir::cleanPath(absolutePath);
    if (!canonicalDir.isEmpty() && !cleanPath.startsWith(canonicalDir)) {
        return QString();
    }
    return cleanPath;
}

QString writeUtf8File(const QString& path, const QString& content)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return QString();
    }
    QTextStream stream(&file);
    stream.setCodec("UTF-8");
    stream << content;
    return path;
}

QJsonObject parseRecordJson(const QString& jsonText)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(jsonText.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return {};
    }
    return document.object();
}

QString recordComplaint(const QJsonObject& object)
{
    return object.value(QStringLiteral("complaint")).toString(
        object.value(QStringLiteral("chief_complaint")).toString(QStringLiteral("待患者表达后自动生成。")));
}

bool isRecognizedWordText(const QString& text)
{
    return !text.isEmpty() &&
            text != QStringLiteral("等待中...") &&
            text != QStringLiteral("识别中...") &&
            text != QStringLiteral("请患者站在识别框内") &&
            text != QStringLiteral("等待手部进入识别区") &&
            !text.contains(QStringLiteral("点击开始按钮")) &&
            !text.contains(QStringLiteral("点击绿色按钮"));
}

}  // namespace

SignController::SignController(QObject* parent)
    : QObject(parent)
{
    connect(&worker_, &QProcess::readyReadStandardOutput, this, &SignController::readStdout);
    connect(&worker_, &QProcess::readyReadStandardError, this, &SignController::readStderr);
    connect(&preview_timer_, &QTimer::timeout, this, &SignController::updateLocalPreview);
    connect(&diagnostics_timer_, &QTimer::timeout, this, &SignController::logDiagnostics);
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

void SignController::setCameraView(CameraView* cameraView)
{
    camera_view_ = cameraView;
}

void SignController::setDoctorCameraView(CameraView* cameraView)
{
    doctor_camera_view_ = cameraView;
}

void SignController::bindDoctorCameraView(QObject* cameraView)
{
    doctor_camera_view_ = qobject_cast<CameraView*>(cameraView);
}

QString SignController::saveMedicalRecord(const QString& jsonText,
                                          const QString& plainText,
                                          const QString& existingPath)
{
    QJsonObject object = parseRecordJson(jsonText);
    if (object.isEmpty() && plainText.trimmed().isEmpty()) {
        return QString();
    }

    const QDir dir = recordsDirectory();
    const QString now = displayTimestamp();
    object.insert(QStringLiteral("status"), QStringLiteral("已保存"));
    object.insert(QStringLiteral("updated_at"), now);
    if (!object.contains(QStringLiteral("created_at"))) {
        object.insert(QStringLiteral("created_at"), now);
    }
    object.insert(QStringLiteral("saved_at"), now);
    object.insert(QStringLiteral("plain_text"), plainText);

    QString jsonPath = safeRecordPath(existingPath);
    if (jsonPath.isEmpty()) {
        jsonPath = dir.absoluteFilePath(QStringLiteral("record_%1.json").arg(recordTimestamp()));
    }
    const QString txtPath = QFileInfo(jsonPath).absolutePath() + QLatin1Char('/') +
        QFileInfo(jsonPath).completeBaseName() + QStringLiteral(".txt");

    QFile jsonFile(jsonPath);
    if (!jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        appendLog(QStringLiteral("record save failed: %1").arg(jsonPath));
        return QString();
    }
    jsonFile.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
    jsonFile.close();

    if (writeUtf8File(txtPath, plainText).isEmpty()) {
        appendLog(QStringLiteral("record text save failed: %1").arg(txtPath));
    }
    appendLog(QStringLiteral("record saved: %1").arg(jsonPath));
    return jsonPath;
}

QString SignController::exportMedicalRecord(const QString& jsonText,
                                            const QString& plainText,
                                            const QString& existingPath)
{
    QJsonObject object = parseRecordJson(jsonText);
    const QString jsonPath = safeRecordPath(existingPath);
    if (jsonPath.isEmpty()) {
        appendLog(QStringLiteral("record export rejected: record is not saved"));
        return QString();
    }

    const QString now = displayTimestamp();
    const QFileInfo info(jsonPath);
    const QString path = info.absolutePath() + QLatin1Char('/') +
        info.completeBaseName() + QStringLiteral("_export.txt");
    QString content = plainText;
    if (content.trimmed().isEmpty() && !object.isEmpty()) {
        content = QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented));
    }
    if (writeUtf8File(path, content).isEmpty()) {
        appendLog(QStringLiteral("record export failed: %1").arg(path));
        return QString();
    }

    if (!object.isEmpty()) {
        object.insert(QStringLiteral("status"), QStringLiteral("已导出"));
        object.insert(QStringLiteral("exported_at"), now);
        object.insert(QStringLiteral("updated_at"), now);
        object.insert(QStringLiteral("plain_text"), plainText);
        QFile jsonFile(jsonPath);
        if (jsonFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            jsonFile.write(QJsonDocument(object).toJson(QJsonDocument::Indented));
        }
    }

    appendLog(QStringLiteral("record exported: %1").arg(path));
    return path;
}

QVariantList SignController::loadMedicalRecords() const
{
    QVariantList records;
    const QDir dir = recordsDirectory();
    const QFileInfoList files = dir.entryInfoList(
        QStringList() << QStringLiteral("record_*.json"),
        QDir::Files,
        QDir::Time);

    for (const QFileInfo& fileInfo : files) {
        QFile file(fileInfo.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !document.isObject()) {
            continue;
        }

        const QJsonObject object = document.object();
        QVariantMap record;
        const QString savedAt = object.value(QStringLiteral("saved_at")).toString();
        const QString exportedAt = object.value(QStringLiteral("exported_at")).toString();
        const QString updatedAt = object.value(QStringLiteral("updated_at")).toString();
        const QString createdAt = object.value(QStringLiteral("created_at")).toString();
        record.insert(QStringLiteral("time"),
                      !exportedAt.isEmpty() ? exportedAt :
                      (!savedAt.isEmpty() ? savedAt :
                      (!updatedAt.isEmpty() ? updatedAt : createdAt)));
        record.insert(QStringLiteral("status"), object.value(QStringLiteral("status")).toString(QStringLiteral("已保存")));
        record.insert(QStringLiteral("complaint"), recordComplaint(object));
        record.insert(QStringLiteral("saved_at"), savedAt);
        record.insert(QStringLiteral("exported_at"), exportedAt);
        record.insert(QStringLiteral("updated_at"), updatedAt);
        record.insert(QStringLiteral("created_at"), createdAt);
        record.insert(QStringLiteral("plain_text"), object.value(QStringLiteral("plain_text")).toString());
        record.insert(QStringLiteral("name"), object.value(QStringLiteral("name")).toString());
        record.insert(QStringLiteral("gender"), object.value(QStringLiteral("gender")).toString());
        record.insert(QStringLiteral("age"), object.value(QStringLiteral("age")).toString());
        record.insert(QStringLiteral("record_no"), object.value(QStringLiteral("record_no")).toString());
        record.insert(QStringLiteral("current_history"), object.value(QStringLiteral("current_history")).toString());
        record.insert(QStringLiteral("doctor_note"), object.value(QStringLiteral("doctor_note")).toString());
        record.insert(QStringLiteral("treatment_plan"), object.value(QStringLiteral("treatment_plan")).toString());
        record.insert(QStringLiteral("doctor_signature"), object.value(QStringLiteral("doctor_signature")).toString());
        record.insert(QStringLiteral("path"), fileInfo.absoluteFilePath());
        records.append(record);
    }
    return records;
}

bool SignController::deleteMedicalRecord(const QString& path)
{
    const QString jsonPath = safeRecordPath(path);
    if (jsonPath.isEmpty()) {
        return false;
    }
    const QFileInfo info(jsonPath);
    const QString txtPath = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral(".txt");
    const QString exportPath = info.absolutePath() + QLatin1Char('/') + info.completeBaseName() + QStringLiteral("_export.txt");
    QFile::remove(txtPath);
    QFile::remove(exportPath);
    const bool removed = QFile::remove(jsonPath);
    if (removed) {
        appendLog(QStringLiteral("record deleted: %1").arg(jsonPath));
    }
    return removed;
}

void SignController::startSystem()
{
    if (worker_.state() != QProcess::NotRunning) {
        appendLog(QStringLiteral("worker already running"));
        return;
    }

    intentional_stop_ = false;
    stdout_buffer_.clear();
    stderr_buffer_.clear();
    current_word_ = QStringLiteral("点击开始按钮开始识别");
    confidence_text_ = QStringLiteral("置信度 -");
    fps_text_ = QStringLiteral("FPS -");
    latency_text_ = QStringLiteral("延迟 -");
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：-");
    composer_text_ = QStringLiteral("整理来源：-");
    recording_active_.store(false);
    has_collected_tokens_ = false;
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
    diagnostics_timer_.start(3000);
    setRunning(true);
    setStatus(QStringLiteral("点击开始按钮开始识别"));
}

void SignController::stopSystem()
{
    recording_active_.store(false);
    intentional_stop_ = true;
    if (worker_.state() != QProcess::NotRunning) {
        worker_.terminate();
        if (!worker_.waitForFinished(1500)) {
            worker_.kill();
            worker_.waitForFinished(1500);
        }
    } else {
        intentional_stop_ = false;
    }
    diagnostics_timer_.stop();
    stopLocalPreview();
    setRunning(false);
    setStatus(QStringLiteral("已停止"));
}

void SignController::startRecording()
{
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：表达中...");
    composer_text_ = QStringLiteral("整理来源：-");
    current_word_ = QStringLiteral("等待中...");
    recording_active_.store(true);
    has_collected_tokens_ = false;
    setProgress(0);
    setStatus(QStringLiteral("等待手部"));
    emit currentWordChanged();
    emit tokensChanged();
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("start_recording"));
}

void SignController::stopRecording()
{
    if (!has_collected_tokens_) {
        sentence_text_ = QStringLiteral("整理句子：请先进行手语识别后再按结束按钮");
        setStatus(QStringLiteral("请先完成至少一个词汇识别"));
        emit sentenceChanged();
        appendLog(QStringLiteral("stop ignored: no recognized tokens"));
        return;
    }

    recording_active_.store(false);
    sentence_text_ = QStringLiteral("整理句子：正在整理...");
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("stop_recording"));
}

void SignController::deleteLastToken()
{
    if (!has_collected_tokens_) {
        sentence_text_ = QStringLiteral("整理句子：暂无可删除词汇");
        setStatus(QStringLiteral("暂无可删除词汇"));
        emit sentenceChanged();
        appendLog(QStringLiteral("delete ignored: no recognized tokens"));
        return;
    }

    sentence_text_ = recording_active_.load()
        ? QStringLiteral("整理句子：继续识别中...")
        : QStringLiteral("整理句子：-");
    composer_text_ = QStringLiteral("整理来源：-");
    emit sentenceChanged();
    sendWorkerCommand(QStringLiteral("delete_last"));
}

void SignController::clearExpression()
{
    recording_active_.store(false);
    has_collected_tokens_ = false;
    current_word_ = worker_.state() == QProcess::NotRunning
        ? QStringLiteral("等待中...")
        : QStringLiteral("点击开始按钮开始识别");
    tokens_text_ = QStringLiteral("已收集词汇：-");
    sentence_text_ = QStringLiteral("整理句子：-");
    composer_text_ = QStringLiteral("整理来源：-");
    setProgress(0);
    setStatus(worker_.state() == QProcess::NotRunning ? QStringLiteral("未启动") : QStringLiteral("点击开始按钮开始识别"));
    emit currentWordChanged();
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

    int worker_frame_id = 0;
    QByteArray worker_frame_line;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        worker_frame_id = latest_worker_frame_id_;
        if (recording_active_.load() && worker_frame_id != sent_worker_frame_id_) {
            worker_frame_line = latest_worker_frame_line_;
        }
    }

    if (!worker_frame_line.isEmpty() && worker_.state() != QProcess::NotRunning) {
        if (worker_.bytesToWrite() <= 512 * 1024) {
            worker_.write(worker_frame_line);
            sent_worker_frame_id_ = worker_frame_id;
            worker_frames_sent_.fetch_add(1, std::memory_order_relaxed);
        } else {
            worker_frames_backpressure_.fetch_add(1, std::memory_order_relaxed);
        }
    }
}

void SignController::logDiagnostics()
{
    int latest_worker_frame_id = 0;
    int sent_worker_frame_id = 0;
    int worker_frame_width = 0;
    int worker_frame_height = 0;
    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        latest_worker_frame_id = latest_worker_frame_id_;
        sent_worker_frame_id = sent_worker_frame_id_;
        worker_frame_width = latest_worker_frame_width_;
        worker_frame_height = latest_worker_frame_height_;
    }

    appendLog(
        QStringLiteral(
            "diag ui=alive running=%1 recording=%2 worker_state=%3 stdin_bytes=%4 capture=%5 preview_ok=%6 preview_drop=%7 encoded=%8 sent=%9 backpressure=%10 worker_latest=%11 worker_sent_id=%12 results=%13 recog_size=%14x%15 sequence_frames=%16 window=%17s lost=%18")
            .arg(running_ ? 1 : 0)
            .arg(recording_active_.load() ? 1 : 0)
            .arg(static_cast<int>(worker_.state()))
            .arg(worker_.bytesToWrite())
            .arg(capture_frames_.load(std::memory_order_relaxed))
            .arg(preview_delivered_.load(std::memory_order_relaxed))
            .arg(preview_dropped_.load(std::memory_order_relaxed))
            .arg(worker_frames_encoded_.load(std::memory_order_relaxed))
            .arg(worker_frames_sent_.load(std::memory_order_relaxed))
            .arg(worker_frames_backpressure_.load(std::memory_order_relaxed))
            .arg(latest_worker_frame_id)
            .arg(sent_worker_frame_id)
            .arg(worker_results_.load(std::memory_order_relaxed))
            .arg(worker_frame_width)
            .arg(worker_frame_height)
            .arg(latest_sequence_frames_.load(std::memory_order_relaxed))
            .arg(latest_window_sec_milli_.load(std::memory_order_relaxed) / 1000.0, 0, 'f', 2)
            .arg(latest_hand_lost_frames_.load(std::memory_order_relaxed)));
}

void SignController::handleFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    Q_UNUSED(exitStatus);
    diagnostics_timer_.stop();
    stopLocalPreview();
    setRunning(false);
    setStatus(QStringLiteral("已停止"));
    intentional_stop_ = false;
    appendLog(QStringLiteral("worker finished, exitCode=%1").arg(exitCode));
}

void SignController::handleError(QProcess::ProcessError error)
{
    stopLocalPreview();
    diagnostics_timer_.stop();
    setRunning(false);
    setStatus(intentional_stop_ ? QStringLiteral("已停止") : QStringLiteral("错误"));
    intentional_stop_ = false;
    appendLog(QStringLiteral("worker error: %1").arg(static_cast<int>(error)));
}

void SignController::appendLog(const QString& line)
{
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
    const QString message = QStringLiteral("[%1] %2").arg(stamp, line);
    std::cout << message.toStdString() << std::endl;
    if (line.startsWith(QStringLiteral("diag "))) {
        return;
    }
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
        current_word_ = QStringLiteral("点击开始按钮开始识别");
        setStatus(QStringLiteral("点击开始按钮开始识别"));
        emit currentWordChanged();
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
        recording_active_.store(active);
        const QStringList tokens = jsonTokens(object.value(QStringLiteral("tokens")).toArray());
        has_collected_tokens_ = !tokens.isEmpty();
        if (has_collected_tokens_) {
            setProgress(100);
        } else {
            setProgress(0);
        }
        setStatus(active ? QStringLiteral("表达采集中") : QStringLiteral("点击开始按钮开始识别"));
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
        return;
    }

    if (type == QStringLiteral("tokens")) {
        has_collected_tokens_ = !jsonTokens(object.value(QStringLiteral("tokens")).toArray()).isEmpty();
        if (has_collected_tokens_) {
            setProgress(100);
        } else {
            setProgress(0);
        }
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
        return;
    }

    if (type == QStringLiteral("sentence")) {
        has_collected_tokens_ = !jsonTokens(object.value(QStringLiteral("tokens")).toArray()).isEmpty();
        if (has_collected_tokens_) {
            setProgress(100);
        } else {
            setProgress(0);
        }
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

    worker_results_.fetch_add(1, std::memory_order_relaxed);
    current_word_ = object.value(QStringLiteral("text")).toString(QStringLiteral("识别中..."));
    confidence_text_ = QStringLiteral("置信度 %1").arg(jsonNumber(object, QStringLiteral("confidence")));
    fps_text_ = QStringLiteral("FPS %1").arg(jsonNumber(object, QStringLiteral("fps")));
    latency_text_ = latencySummary(object.value(QStringLiteral("latency_ms")).toObject());
    emit currentWordChanged();
    emit metricsChanged();

    if (object.contains(QStringLiteral("tokens"))) {
        has_collected_tokens_ = !jsonTokens(object.value(QStringLiteral("tokens")).toArray()).isEmpty();
        tokens_text_ = tokensDisplayText(object.value(QStringLiteral("tokens")).toArray());
        emit tokensChanged();
    }

    const QString sentence = object.value(QStringLiteral("sentence")).toString();
    if (!sentence.isEmpty()) {
        sentence_text_ = QStringLiteral("整理句子：%1").arg(sentence);
        emit sentenceChanged();
    }

    {
        std::lock_guard<std::mutex> lock(preview_mutex_);
        const auto readBox = [](const QJsonValue& value, double* out) -> bool {
            if (!value.isObject()) {
                return false;
            }
            const QJsonObject box = value.toObject();
            out[0] = box.value(QStringLiteral("x_min")).toDouble(0.15);
            out[1] = box.value(QStringLiteral("y_min")).toDouble(0.0);
            out[2] = box.value(QStringLiteral("x_max")).toDouble(0.85);
            out[3] = box.value(QStringLiteral("y_max")).toDouble(1.0);
            return true;
        };
        has_person_bbox_ = readBox(object.value(QStringLiteral("person_bbox")), person_bbox_);
        has_body_roi_ = readBox(object.value(QStringLiteral("body_roi")), body_roi_);
        has_dynamic_roi_ = readBox(object.value(QStringLiteral("roi_box")), dynamic_roi_);
    }

    const QString tracking_status = object.value(QStringLiteral("tracking_status")).toString();
    const bool hands_valid = object.value(QStringLiteral("hands_valid")).toBool(false);
    const int sequence_frames = object.value(QStringLiteral("sequence_frames")).toInt(0);
    const int hand_lost_frames = object.value(QStringLiteral("hand_lost_frames")).toInt(0);
    const double window_sec = object.value(QStringLiteral("window_sec")).toDouble(0.0);
    latest_sequence_frames_.store(sequence_frames, std::memory_order_relaxed);
    latest_hand_lost_frames_.store(hand_lost_frames, std::memory_order_relaxed);
    latest_window_sec_milli_.store(static_cast<int>(window_sec * 1000.0), std::memory_order_relaxed);
    const int window_progress = qBound(0, static_cast<int>((window_sec / 1.5) * 100.0), 100);
    setProgress(isRecognizedWordText(current_word_) ? 100 : window_progress);
    if (!tracking_status.isEmpty()) {
        setStatus(tracking_status);
        return;
    }
    setStatus(
        QStringLiteral("%1 | frames %2 | window %3s | lost %4")
            .arg(hands_valid ? QStringLiteral("追踪中") : QStringLiteral("等待手部"))
            .arg(sequence_frames)
            .arg(window_sec, 0, 'f', 2)
            .arg(hand_lost_frames));
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
        latest_worker_frame_line_.clear();
        latest_worker_frame_id_ = 0;
        sent_worker_frame_id_ = 0;
        latest_worker_frame_width_ = 0;
        latest_worker_frame_height_ = 0;
        has_person_bbox_ = false;
        has_body_roi_ = false;
        has_dynamic_roi_ = false;
    }
    capture_frames_.store(0, std::memory_order_relaxed);
    preview_delivered_.store(0, std::memory_order_relaxed);
    preview_dropped_.store(0, std::memory_order_relaxed);
    worker_frames_encoded_.store(0, std::memory_order_relaxed);
    worker_frames_sent_.store(0, std::memory_order_relaxed);
    worker_frames_backpressure_.store(0, std::memory_order_relaxed);
    worker_results_.store(0, std::memory_order_relaxed);
    preview_update_pending_.store(false);
    preview_stop_.store(false);
    local_preview_active_ = true;
    preview_thread_ = std::thread(&SignController::capturePreviewFrames, this, camera_id, width, height);
    preview_timer_.start(50);
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
        latest_worker_frame_line_.clear();
        latest_worker_frame_id_ = 0;
        sent_worker_frame_id_ = 0;
        latest_worker_frame_width_ = 0;
        latest_worker_frame_height_ = 0;
        has_person_bbox_ = false;
        has_body_roi_ = false;
        has_dynamic_roi_ = false;
    }
    preview_update_pending_.store(false);
    local_preview_active_ = false;
    if (camera_view_) {
        camera_view_->clearFrame();
    }
    if (doctor_camera_view_) {
        doctor_camera_view_->clearFrame();
    }
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
                setStatus(QStringLiteral("错误"));
                stopLocalPreview();
            },
            Qt::QueuedConnection);
        return;
    }

    cap.set(cv::CAP_PROP_FRAME_WIDTH, width);
    cap.set(cv::CAP_PROP_FRAME_HEIGHT, height);
    cap.set(cv::CAP_PROP_FPS, 30);
    cap.set(cv::CAP_PROP_BUFFERSIZE, 1);
    // ========== 切换自动曝光（画面自动明暗调节）==========
    // cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 3);

    // ========== 切换手动曝光（固定亮度，不会忽亮忽暗）==========
    cap.set(cv::CAP_PROP_AUTO_EXPOSURE, 1);
    cap.set(cv::CAP_PROP_EXPOSURE, 200);
    // cap.set(cv::CAP_PROP_GAIN, 0);
    QMetaObject::invokeMethod(
        this,
        [this, exposure = cap.get(cv::CAP_PROP_EXPOSURE), gain = cap.get(cv::CAP_PROP_GAIN)]() {
            appendLog(QStringLiteral("Qt camera exposure=%1 gain=%2").arg(exposure).arg(gain));
        },
        Qt::QueuedConnection);

    auto last_worker_encode_time = std::chrono::steady_clock::now() - std::chrono::milliseconds(100);

    while (!preview_stop_.load()) {
        cv::Mat frame;
        if (!cap.read(frame) || frame.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        capture_frames_.fetch_add(1, std::memory_order_relaxed);

        if ((camera_view_ || doctor_camera_view_) && !preview_update_pending_.exchange(true)) {
            cv::Mat base_display = frame.clone();
            const int preview_width = 960;
            if (base_display.cols > preview_width) {
                const int preview_height = std::max(1, base_display.rows * preview_width / base_display.cols);
                cv::resize(base_display, base_display, cv::Size(preview_width, preview_height), 0, 0, cv::INTER_AREA);
            }

            cv::Mat patient_display = base_display.clone();
            const int guide_x1 = static_cast<int>(patient_display.cols * 0.15);
            const int guide_x2 = static_cast<int>(patient_display.cols * 0.85);
            cv::rectangle(
                patient_display,
                cv::Point(guide_x1, 0),
                cv::Point(guide_x2, patient_display.rows - 1),
                cv::Scalar(255, 255, 0),
                3);

            cv::Mat doctor_display = base_display.clone();
            bool has_dynamic_roi = false;
            double dynamic_roi[4] = {0.15, 0.0, 0.85, 1.0};
            {
                std::lock_guard<std::mutex> lock(preview_mutex_);
                has_dynamic_roi = has_dynamic_roi_;
                std::copy(std::begin(dynamic_roi_), std::end(dynamic_roi_), std::begin(dynamic_roi));
            }
            const auto drawNormBox = [&doctor_display](const double* box, const cv::Scalar& color, int thickness) {
                const int x1 = static_cast<int>(doctor_display.cols * box[0]);
                const int y1 = static_cast<int>(doctor_display.rows * box[1]);
                const int x2 = static_cast<int>(doctor_display.cols * box[2]);
                const int y2 = static_cast<int>(doctor_display.rows * box[3]);
                cv::rectangle(doctor_display, cv::Point(x1, y1), cv::Point(x2, y2), color, thickness);
            };
            if (has_dynamic_roi) {
                drawNormBox(dynamic_roi, cv::Scalar(255, 0, 0), 3);
            } else {
                cv::rectangle(
                    doctor_display,
                    cv::Point(guide_x1, 0),
                    cv::Point(guide_x2, doctor_display.rows - 1),
                    cv::Scalar(255, 255, 0),
                    2);
            }
            cv::Mat patient_rgb;
            cv::cvtColor(patient_display, patient_rgb, cv::COLOR_BGR2RGB);
            QImage patient_image(
                patient_rgb.data,
                patient_rgb.cols,
                patient_rgb.rows,
                static_cast<int>(patient_rgb.step),
                QImage::Format_RGB888);
            const QImage patient_frame_image = patient_image.copy();

            cv::Mat doctor_rgb;
            cv::cvtColor(doctor_display, doctor_rgb, cv::COLOR_BGR2RGB);
            QImage doctor_image(
                doctor_rgb.data,
                doctor_rgb.cols,
                doctor_rgb.rows,
                static_cast<int>(doctor_rgb.step),
                QImage::Format_RGB888);
            const QImage doctor_frame_image = doctor_image.copy();

            QMetaObject::invokeMethod(
                this,
                [this,
                 patient_view = camera_view_,
                 doctor_view = doctor_camera_view_,
                 patient_frame_image,
                 doctor_frame_image]() {
                    if (local_preview_active_) {
                        if (patient_view) {
                            patient_view->setFrame(patient_frame_image);
                        }
                        if (doctor_view) {
                            doctor_view->setFrame(doctor_frame_image);
                        }
                    }
                    preview_update_pending_.store(false);
                },
                Qt::QueuedConnection);
            preview_delivered_.fetch_add(1, std::memory_order_relaxed);
        } else if (camera_view_ || doctor_camera_view_) {
            preview_dropped_.fetch_add(1, std::memory_order_relaxed);
        }

        QByteArray worker_frame_line;
        int worker_frame_width = 0;
        int worker_frame_height = 0;
        const auto now = std::chrono::steady_clock::now();
        if (recording_active_.load() && now - last_worker_encode_time >= std::chrono::milliseconds(50)) {
            last_worker_encode_time = now;

            cv::Mat send_frame = frame;
            const int recognition_width = 1280;
            if (send_frame.cols > recognition_width) {
                const int recognition_height = std::max(1, send_frame.rows * recognition_width / send_frame.cols);
                cv::Mat resized;
                cv::resize(send_frame, resized, cv::Size(recognition_width, recognition_height), 0, 0, cv::INTER_AREA);
                send_frame = resized;
            }

            std::vector<uchar> encoded;
            if (cv::imencode(".jpg", send_frame, encoded, {cv::IMWRITE_JPEG_QUALITY, 70})) {
                QJsonObject object;
                object.insert(QStringLiteral("cmd"), QStringLiteral("frame"));
                object.insert(
                    QStringLiteral("image"),
                    QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(encoded.data()), static_cast<int>(encoded.size())).toBase64()));
                worker_frame_line = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
                worker_frame_width = send_frame.cols;
                worker_frame_height = send_frame.rows;
                worker_frames_encoded_.fetch_add(1, std::memory_order_relaxed);
            }
        }

        std::lock_guard<std::mutex> lock(preview_mutex_);
        if (!worker_frame_line.isEmpty()) {
            latest_worker_frame_line_ = worker_frame_line;
            ++latest_worker_frame_id_;
            latest_worker_frame_width_ = worker_frame_width;
            latest_worker_frame_height_ = worker_frame_height;
        }
    }
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
    appendLog(QStringLiteral("command: %1, pending stdin bytes: %2").arg(command).arg(worker_.bytesToWrite()));
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
    const QString modelPath = qEnvironmentVariable(
        "SIGN_TCN_MODEL",
        "artifacts/models/sign_tcn_best.rknn");
    const QString featureMode = qEnvironmentVariable("SIGN_FEATURE_MODE", "full");

    return {
        projectPath(QStringLiteral("scripts/infer/infer_realtime_rknn.py")),
        QStringLiteral("--model"), modelPath,
        QStringLiteral("--labels"), QStringLiteral("label_map.json"),
        QStringLiteral("--feature-mode"), featureMode,
        QStringLiteral("--camera"), QStringLiteral("21"),
        QStringLiteral("--width"), QStringLiteral("1280"),
        QStringLiteral("--height"), QStringLiteral("720"),
        QStringLiteral("--pose-interval"), QStringLiteral("4"),
        QStringLiteral("--hand-interval"), QStringLiteral("2"),
        QStringLiteral("--json-interval"), QStringLiteral("0.05"),
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
