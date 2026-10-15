#pragma once

#include <QObject>
#include <QQuickView>
#include <QPoint>
#include <QString>

class SessionDialogController : public QObject
{
    Q_OBJECT

public:
    explicit SessionDialogController(QObject *parent = nullptr);
    ~SessionDialogController() override;

    void showPrompt(const QString &action, const QPoint &cursorPos);
    void hideDialog();

    bool eventFilter(QObject *obj, QEvent *event) override;

private slots:
    void onConfirmed();
    void onCancelled();

private:
    void executeAction(const QString &action);

    QQuickView *m_view = nullptr;
    QString m_currentAction;
};
