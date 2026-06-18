#ifndef SIGN_LANGUAGE_QT_DOCTOR_WINDOW_HPP
#define SIGN_LANGUAGE_QT_DOCTOR_WINDOW_HPP

#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

class DoctorWindow : public QWidget {
    Q_OBJECT

public:
    explicit DoctorWindow(QWidget* parent = nullptr);

    void updateLiveWord(const QString& word, double confidence);
    void updateTokens(const QStringList& tokens);
    void updateSentence(const QString& sentence);
    void clearRecord();

private:
    void buildUi();
    QString inferSymptom(const QStringList& tokens) const;
    QString inferTimeWord(const QStringList& tokens) const;
    void setDoctorReply(const QString& reply);

    QLabel* live_word_label_ = nullptr;
    QLabel* chief_complaint_label_ = nullptr;
    QLabel* tokens_label_ = nullptr;
    QLabel* symptom_label_ = nullptr;
    QLabel* time_label_ = nullptr;
    QTextEdit* doctor_reply_edit_ = nullptr;
};

#endif
