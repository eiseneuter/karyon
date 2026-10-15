#pragma once

#include <QObject>

class VolumeManager : public QObject
{
    Q_OBJECT

public:
    explicit VolumeManager(QObject *parent = nullptr);
    ~VolumeManager() override = default;

    void refresh();
    int volume() const { return m_volume; }
    bool isMuted() const { return m_muted; }

    void stepVolume(int stepPercent);
    void toggleMute();

signals:
    void volumeChanged(int volume, bool isMuted);

private:
    int m_volume = 50;
    bool m_muted = false;
    bool m_hasWpctl = true;
};
