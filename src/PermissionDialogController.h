#pragma once

#include <QObject>
#include <QQuickView>

class PermissionDialogController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool uinputOk READ uinputOk NOTIFY statusChanged)
    Q_PROPERTY(bool eventOk READ eventOk NOTIFY statusChanged)
    Q_PROPERTY(bool allOk READ allOk NOTIFY statusChanged)
    Q_PROPERTY(bool isBusy READ isBusy NOTIFY busyChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)

public:
    explicit PermissionDialogController(QObject *parent = nullptr);
    ~PermissionDialogController() override;

    bool uinputOk() const;
    bool eventOk() const;
    bool allOk() const;
    bool isBusy() const { return m_isBusy; }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE bool grantPermissions();
    Q_INVOKABLE void exitApp();
    Q_INVOKABLE void startWindowDrag();

    bool execDialog();

signals:
    void statusChanged();
    void busyChanged();
    void errorChanged();
    void completed(bool success);

private:
    QQuickView *m_view = nullptr;
    bool m_isBusy = false;
    QString m_errorMessage;
    bool m_success = false;
};
