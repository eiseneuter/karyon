#include "InfoDialogController.h"
#include "ProcessUtils.h"

#include <QQmlContext>
#include <QScreen>
#include <QGuiApplication>
#include <QCursor>
#include <KWindowSystem>
#include <KX11Extras>

InfoDialogController::InfoDialogController(QObject *parent)
    : QObject(parent)
{
}

InfoDialogController::~InfoDialogController()
{
    if (m_view) {
        m_view->close();
        delete m_view;
        m_view = nullptr;
    }
}

void InfoDialogController::startWindowDrag()
{
    if (m_view) {
        m_view->startSystemMove();
    }
}

void InfoDialogController::closeDialog()
{
    if (m_view) {
        m_view->hide();
    }
}

void InfoDialogController::showInfo()
{
    if (!m_view) {
        m_view = new QQuickView();
        m_view->setTitle(QStringLiteral("Karyon Info"));
        m_view->setIcon(ProcessUtils::karyonIcon());
        m_view->setFlags(Qt::Dialog | Qt::FramelessWindowHint);
        m_view->setColor(QColor(Qt::transparent));
        m_view->setResizeMode(QQuickView::SizeRootObjectToView);
        m_view->rootContext()->setContextProperty(QStringLiteral("infoController"), this);
        m_view->setSource(QUrl(QStringLiteral("qrc:/qt/qml/karyon/qml/InfoDialog.qml")));
        m_view->create();

        if (KWindowSystem::isPlatformX11()) {
            KX11Extras::setOnAllDesktops(m_view->winId(), true);
            KX11Extras::setType(m_view->winId(), NET::Dialog);
        }
    }

    const int w = 520;
    const int h = 560;
    m_view->resize(w, h);

    QPoint cursor = QCursor::pos();
    QScreen *screen = QGuiApplication::screenAt(cursor);
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }

    if (screen) {
        QRect geo = screen->geometry();
        m_view->setPosition(geo.x() + (geo.width() - w) / 2, geo.y() + (geo.height() - h) / 2);
    }

    m_view->show();
    m_view->raise();
    m_view->requestActivate();
}
