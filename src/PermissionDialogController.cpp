#include "PermissionDialogController.h"
#include "PermissionManager.h"
#include "ProcessUtils.h"
#include "ThemeManager.h"

#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QEventLoop>
#include <KWindowSystem>
#include <KX11Extras>

PermissionDialogController::PermissionDialogController(QObject *parent)
    : QObject(parent)
{
}

PermissionDialogController::~PermissionDialogController()
{
    if (m_view) {
        m_view->close();
        delete m_view;
        m_view = nullptr;
    }
}

bool PermissionDialogController::uinputOk() const
{
    return PermissionManager::checkUInputAccess();
}

bool PermissionDialogController::eventOk() const
{
    return PermissionManager::checkEventDeviceAccess();
}

bool PermissionDialogController::allOk() const
{
    return PermissionManager::hasRequiredPermissions();
}

bool PermissionDialogController::grantPermissions()
{
    m_isBusy = true;
    m_errorMessage.clear();
    emit busyChanged();
    emit errorChanged();

    QString err;
    bool ok = PermissionManager::grantPermissions(err);

    m_isBusy = false;
    emit busyChanged();

    if (ok) {
        m_success = true;
        emit statusChanged();
        emit completed(true);
        return true;
    } else {
        m_errorMessage = err.isEmpty() ? QStringLiteral("Failed to acquire input permissions.") : err;
        emit errorChanged();
        emit statusChanged();
        return false;
    }
}

void PermissionDialogController::exitApp()
{
    m_success = false;
    emit completed(false);
}

void PermissionDialogController::startWindowDrag()
{
    if (m_view) {
        m_view->startSystemMove();
    }
}

bool PermissionDialogController::execDialog()
{
    m_view = new QQuickView();
    m_view->setTitle(QStringLiteral("Karyon"));
    m_view->setIcon(ProcessUtils::karyonIcon());
    m_view->setFlags(Qt::Dialog | Qt::FramelessWindowHint);
    m_view->setColor(QColor(Qt::transparent));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->rootContext()->setContextProperty(QStringLiteral("permController"), this);
    m_view->rootContext()->setContextProperty(QStringLiteral("karyonTheme"), &ThemeManager::instance());
    m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/PermissionDialog.qml")));
    m_view->create();

    if (KWindowSystem::isPlatformX11()) {
        KX11Extras::setOnAllDesktops(m_view->winId(), true);
        KX11Extras::setType(m_view->winId(), NET::Dialog);
    }

    QScreen *screen = QGuiApplication::primaryScreen();
    if (screen) {
        int w = 460;
        int h = 245;
        m_view->resize(w, h);
        QRect geo = screen->geometry();
        m_view->setPosition(geo.x() + (geo.width() - w) / 2, geo.y() + (geo.height() - h) / 2);
    }
    m_view->show();

    QEventLoop loop;
    connect(this, &PermissionDialogController::completed, &loop, &QEventLoop::quit);
    loop.exec();

    m_view->close();
    delete m_view;
    m_view = nullptr;

    return m_success;
}
