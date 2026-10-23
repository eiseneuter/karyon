#include "VolumeManager.h"
#include "ProcessUtils.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QtGlobal>

VolumeManager::VolumeManager(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void VolumeManager::updateMaxVolume()
{
    bool raiseMax = false;
    QString configPath = QDir::homePath() + QStringLiteral("/.config/plasmaparc");
    if (QFile::exists(configPath)) {
        QFile file(configPath);
        if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QString content = QString::fromUtf8(file.readAll());
            static const QRegularExpression re(QStringLiteral("RaiseMaximumVolume\\s*=\\s*true"), QRegularExpression::CaseInsensitiveOption);
            if (re.match(content).hasMatch()) {
                raiseMax = true;
            }
        }
    }

    if (raiseMax) {
        m_maxVolume = qMax(150, m_volume);
    } else {
        m_maxVolume = qMax(100, m_volume);
    }
}

void VolumeManager::refresh()
{
    // Try wpctl first (PipeWire default)
    QProcess proc;
    proc.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    proc.start(QStringLiteral("wpctl"), {QStringLiteral("get-volume"), QStringLiteral("@DEFAULT_AUDIO_SINK@")});
    if (proc.waitForFinished(100) && proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0) {
        QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
        int volIdx = out.indexOf(QLatin1String("Volume:"));
        if (volIdx >= 0) {
            QString rest = out.mid(volIdx + 7).trimmed();
            int spaceIdx = rest.indexOf(QLatin1Char(' '));
            QString numStr = (spaceIdx >= 0) ? rest.left(spaceIdx) : rest;
            bool ok = false;
            double val = numStr.toDouble(&ok);
            if (ok) {
                m_volume = qMax(0, qRound(val * 100.0));
            }
            m_muted = out.contains(QLatin1String("[MUTED]"));
            m_hasWpctl = true;
            updateMaxVolume();
            emit volumeChanged(m_volume, m_muted, m_maxVolume);
            return;
        }
    }

    // Fallback: pactl
    m_hasWpctl = false;
    QProcess pactlVol;
    pactlVol.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    pactlVol.start(QStringLiteral("pactl"), {QStringLiteral("get-sink-volume"), QStringLiteral("@DEFAULT_SINK@")});
    if (pactlVol.waitForFinished(100) && pactlVol.exitStatus() == QProcess::NormalExit) {
        QString out = QString::fromUtf8(pactlVol.readAllStandardOutput());
        int pctIdx = out.indexOf(QLatin1Char('%'));
        if (pctIdx > 0) {
            int start = pctIdx - 1;
            while (start >= 0 && out.at(start).isDigit()) {
                start--;
            }
            QString pctStr = out.mid(start + 1, pctIdx - (start + 1));
            bool ok = false;
            int vol = pctStr.toInt(&ok);
            if (ok) {
                m_volume = qMax(0, vol);
            }
        }
    }

    QProcess pactlMute;
    pactlMute.setProcessEnvironment(ProcessUtils::cleanHostEnvironment());
    pactlMute.start(QStringLiteral("pactl"), {QStringLiteral("get-sink-mute"), QStringLiteral("@DEFAULT_SINK@")});
    if (pactlMute.waitForFinished(100) && pactlMute.exitStatus() == QProcess::NormalExit) {
        QString out = QString::fromUtf8(pactlMute.readAllStandardOutput()).toLower();
        m_muted = out.contains(QLatin1String("yes"));
    }

    updateMaxVolume();
    emit volumeChanged(m_volume, m_muted, m_maxVolume);
}

void VolumeManager::stepVolume(int stepPercent)
{
    updateMaxVolume();
    int limit = m_maxVolume;
    m_volume = qBound(0, m_volume + stepPercent, limit);
    emit volumeChanged(m_volume, m_muted, m_maxVolume);

    if (m_hasWpctl) {
        QString arg = QString::number(qAbs(stepPercent)) + QStringLiteral("%") + (stepPercent >= 0 ? QStringLiteral("+") : QStringLiteral("-"));
        QString limitStr = QString::number(limit / 100.0, 'f', 2);
        ProcessUtils::startDetachedHost(QStringLiteral("wpctl"), {
            QStringLiteral("set-volume"),
            QStringLiteral("-l"),
            limitStr,
            QStringLiteral("@DEFAULT_AUDIO_SINK@"),
            arg
        });
    } else {
        if (m_volume >= limit && stepPercent > 0) {
            ProcessUtils::startDetachedHost(QStringLiteral("pactl"), {
                QStringLiteral("set-sink-volume"),
                QStringLiteral("@DEFAULT_SINK@"),
                QStringLiteral("%1%").arg(limit)
            });
        } else {
            QString arg = (stepPercent >= 0 ? QStringLiteral("+") : QStringLiteral("-")) + QString::number(qAbs(stepPercent)) + QStringLiteral("%");
            ProcessUtils::startDetachedHost(QStringLiteral("pactl"), {
                QStringLiteral("set-sink-volume"),
                QStringLiteral("@DEFAULT_SINK@"),
                arg
            });
        }
    }
}

void VolumeManager::toggleMute()
{
    m_muted = !m_muted;
    emit volumeChanged(m_volume, m_muted, m_maxVolume);

    if (m_hasWpctl) {
        ProcessUtils::startDetachedHost(QStringLiteral("wpctl"), {
            QStringLiteral("set-mute"),
            QStringLiteral("@DEFAULT_AUDIO_SINK@"),
            QStringLiteral("toggle")
        });
    } else {
        ProcessUtils::startDetachedHost(QStringLiteral("pactl"), {
            QStringLiteral("set-sink-mute"),
            QStringLiteral("@DEFAULT_SINK@"),
            QStringLiteral("toggle")
        });
    }
}
