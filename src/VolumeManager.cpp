#include "VolumeManager.h"
#include "ProcessUtils.h"

#include <QProcess>
#include <QtGlobal>

VolumeManager::VolumeManager(QObject *parent)
    : QObject(parent)
{
    refresh();
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
                m_volume = qBound(0, qRound(val * 100.0), 100);
            }
            m_muted = out.contains(QLatin1String("[MUTED]"));
            m_hasWpctl = true;
            emit volumeChanged(m_volume, m_muted);
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
                m_volume = qBound(0, vol, 100);
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

    emit volumeChanged(m_volume, m_muted);
}

void VolumeManager::stepVolume(int stepPercent)
{
    m_volume = qBound(0, m_volume + stepPercent, 100);
    emit volumeChanged(m_volume, m_muted);

    if (m_hasWpctl) {
        QString arg = QString::number(qAbs(stepPercent)) + QStringLiteral("%") + (stepPercent >= 0 ? QStringLiteral("+") : QStringLiteral("-"));
        ProcessUtils::startDetachedHost(QStringLiteral("wpctl"), {
            QStringLiteral("set-volume"),
            QStringLiteral("-l"),
            QStringLiteral("1.0"),
            QStringLiteral("@DEFAULT_AUDIO_SINK@"),
            arg
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

void VolumeManager::toggleMute()
{
    m_muted = !m_muted;
    emit volumeChanged(m_volume, m_muted);

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
