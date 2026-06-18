#include "DoctorWindow.hpp"

#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QTextEdit>
#include <QVBoxLayout>

DoctorWindow::DoctorWindow(QWidget* parent)
    : QWidget(parent)
{
    buildUi();
}

void DoctorWindow::buildUi()
{
    setWindowTitle(QStringLiteral("医生端电子病历"));

    auto* root_layout = new QVBoxLayout(this);
    root_layout->setSpacing(14);

    auto* title = new QLabel(QStringLiteral("门诊无障碍沟通记录"), this);
    title->setAlignment(Qt::AlignCenter);
    title->setMinimumHeight(58);
    title->setStyleSheet(QStringLiteral("font-size: 30px; font-weight: 700; color: #1d3557;"));

    live_word_label_ = new QLabel(QStringLiteral("实时识别：-"), this);
    live_word_label_->setMinimumHeight(54);
    live_word_label_->setAlignment(Qt::AlignCenter);
    live_word_label_->setStyleSheet(QStringLiteral("font-size: 22px; background: #f8fbff; border: 1px solid #cbd8e6;"));

    chief_complaint_label_ = new QLabel(QStringLiteral("患者主诉：-"), this);
    chief_complaint_label_->setWordWrap(true);
    chief_complaint_label_->setMinimumHeight(120);
    chief_complaint_label_->setAlignment(Qt::AlignCenter);
    chief_complaint_label_->setStyleSheet(QStringLiteral("font-size: 34px; font-weight: 650; color: #0b4f6c; background: #eef8fb; border: 1px solid #b6d6e0;"));

    auto* info_box = new QGroupBox(QStringLiteral("结构化信息"), this);
    auto* info_layout = new QGridLayout(info_box);
    tokens_label_ = new QLabel(QStringLiteral("识别词：-"), info_box);
    symptom_label_ = new QLabel(QStringLiteral("症状：待确认"), info_box);
    time_label_ = new QLabel(QStringLiteral("时间：待确认"), info_box);
    for (QLabel* label : {tokens_label_, symptom_label_, time_label_}) {
        label->setMinimumHeight(42);
        label->setStyleSheet(QStringLiteral("font-size: 20px; padding: 6px; background: white; border: 1px solid #d9dde3;"));
    }
    info_layout->addWidget(tokens_label_, 0, 0, 1, 2);
    info_layout->addWidget(symptom_label_, 1, 0);
    info_layout->addWidget(time_label_, 1, 1);

    auto* reply_box = new QGroupBox(QStringLiteral("医生快捷回复"), this);
    auto* reply_layout = new QGridLayout(reply_box);
    const QStringList replies = {
        QStringLiteral("请问哪里不舒服？"),
        QStringLiteral("这种情况持续多久了？"),
        QStringLiteral("有没有发烧？"),
        QStringLiteral("有没有咳嗽？"),
        QStringLiteral("请稍等，我帮您登记。"),
        QStringLiteral("请到诊室进一步检查。"),
    };
    for (int i = 0; i < replies.size(); ++i) {
        auto* button = new QPushButton(replies[i], reply_box);
        button->setMinimumHeight(54);
        button->setStyleSheet(QStringLiteral("font-size: 20px;"));
        connect(button, &QPushButton::clicked, this, [this, text = replies[i]] {
            setDoctorReply(text);
        });
        reply_layout->addWidget(button, i / 2, i % 2);
    }

    doctor_reply_edit_ = new QTextEdit(this);
    doctor_reply_edit_->setReadOnly(true);
    doctor_reply_edit_->setMinimumHeight(110);
    doctor_reply_edit_->setStyleSheet(QStringLiteral("font-size: 24px; background: #fffdf7; border: 1px solid #e0d7aa;"));
    doctor_reply_edit_->setText(QStringLiteral("医生回复：-"));

    root_layout->addWidget(title);
    root_layout->addWidget(live_word_label_);
    root_layout->addWidget(chief_complaint_label_);
    root_layout->addWidget(info_box);
    root_layout->addWidget(reply_box);
    root_layout->addWidget(doctor_reply_edit_);
}

void DoctorWindow::updateLiveWord(const QString& word, double confidence)
{
    live_word_label_->setText(QStringLiteral("实时识别：%1    置信度：%2").arg(word).arg(confidence, 0, 'f', 2));
}

void DoctorWindow::updateTokens(const QStringList& tokens)
{
    tokens_label_->setText(tokens.isEmpty() ? QStringLiteral("识别词：-") : QStringLiteral("识别词：%1").arg(tokens.join(QStringLiteral(" / "))));
    symptom_label_->setText(QStringLiteral("症状：%1").arg(inferSymptom(tokens)));
    time_label_->setText(QStringLiteral("时间：%1").arg(inferTimeWord(tokens)));
}

void DoctorWindow::updateSentence(const QString& sentence)
{
    chief_complaint_label_->setText(sentence.isEmpty() ? QStringLiteral("患者主诉：-") : QStringLiteral("患者主诉：%1").arg(sentence));
}

void DoctorWindow::clearRecord()
{
    updateLiveWord(QStringLiteral("-"), 0.0);
    updateTokens({});
    updateSentence({});
    doctor_reply_edit_->setText(QStringLiteral("医生回复：-"));
}

QString DoctorWindow::inferSymptom(const QStringList& tokens) const
{
    if (tokens.contains(QStringLiteral("发烧"))) {
        return QStringLiteral("发烧");
    }
    if (tokens.contains(QStringLiteral("咳嗽"))) {
        return QStringLiteral("咳嗽");
    }
    if (tokens.contains(QStringLiteral("头")) && tokens.contains(QStringLiteral("疼痛"))) {
        return QStringLiteral("头疼");
    }
    if (tokens.contains(QStringLiteral("疼痛"))) {
        return QStringLiteral("疼痛");
    }
    return QStringLiteral("待确认");
}

QString DoctorWindow::inferTimeWord(const QStringList& tokens) const
{
    if (tokens.contains(QStringLiteral("昨天"))) {
        return QStringLiteral("昨天");
    }
    return QStringLiteral("待确认");
}

void DoctorWindow::setDoctorReply(const QString& reply)
{
    doctor_reply_edit_->setText(QStringLiteral("医生回复：%1").arg(reply));
}
