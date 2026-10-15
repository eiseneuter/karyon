#pragma once

#include <QObject>
#include <QQuickView>

class InfoDialogController : public QObject
{
    Q_OBJECT

public:
    explicit InfoDialogController(QObject *parent = nullptr);
    ~InfoDialogController() override;

    Q_INVOKABLE void startWindowDrag();
    Q_INVOKABLE void closeDialog();

    void showInfo();

private:
    QQuickView *m_view = nullptr;
};
