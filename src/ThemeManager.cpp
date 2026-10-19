#include "ThemeManager.h"
#include "SettingsManager.h"

#include <algorithm>
#include <cmath>

ThemeManager &ThemeManager::instance()
{
    static ThemeManager s_instance;
    return s_instance;
}

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent)
{
    // Connect to settings changes
    connect(&SettingsManager::instance(), &SettingsManager::settingsChanged, this, [this]() {
        applyTheme(colorSchemeIndex(), customBaseColorHex());
    });

    // Initialize with current settings
    applyTheme(colorSchemeIndex(), customBaseColorHex());
}

int ThemeManager::colorSchemeIndex() const
{
    return SettingsManager::instance().colorSchemeIndex();
}

QString ThemeManager::customBaseColorHex() const
{
    return SettingsManager::instance().customBaseColorHex();
}

void ThemeManager::applyTheme(int schemeIndex, const QString &customHex)
{
    switch (schemeIndex) {
    case 0:
        loadCharcoalPreset();
        break;
    case 1:
        loadSlatePreset();
        break;
    case 2:
        loadEmeraldPreset();
        break;
    case 3:
        loadAmethystPreset();
        break;
    case 4:
        loadCappuccinoPreset();
        break;
    case 5:
        loadCrimsonPreset();
        break;
    case 6:
    default: {
        QColor base(customHex.isEmpty() ? QStringLiteral("#22262d") : customHex);
        if (!base.isValid()) {
            base = QColor(QStringLiteral("#22262d"));
        }
        generatePalette(base);
        break;
    }
    }

    emit themeChanged();
}

void ThemeManager::loadCharcoalPreset()
{
    m_theme.isLight = false;

    // Classic Karyon Dark Carbon Charcoal & Cobalt (Default)
    m_theme.baseBg = QColor(QStringLiteral("#22262d"));
    m_theme.cardBg = QColor(QStringLiteral("#1e2127"));
    m_theme.cardBorder = QColor(QStringLiteral("#383f4b"));
    m_theme.dividerColor = QColor(QStringLiteral("#2a2f38"));
    m_theme.controlBg = QColor(QStringLiteral("#2a2f38"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#373e4a"));
    m_theme.controlBorder = QColor(QStringLiteral("#454e5d"));

    m_theme.ring1Bg = QColor(QStringLiteral("#22262d"));
    m_theme.ring1Border = QColor(QStringLiteral("#17191e"));
    m_theme.ring2Bg = QColor(QStringLiteral("#1e2126"));
    m_theme.ring2Border = QColor(QStringLiteral("#14161a"));
    m_theme.ring3Bg = QColor(QStringLiteral("#1a1d22"));
    m_theme.ring3Border = QColor(QStringLiteral("#111316"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#434b58"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#556071"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#353c48"));

    m_theme.accentColor = QColor(QStringLiteral("#0d7cd6"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#298fe6"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(115);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentHoverColor;

    m_theme.identWindows = QColor(QStringLiteral("#2ed573"));
    m_theme.identApps = QColor(QStringLiteral("#37d0ff"));
    m_theme.identFiles = QColor(QStringLiteral("#ff9f43"));

    m_theme.textPrimary = QColor(QStringLiteral("#ffffff"));
    m_theme.textSecondary = QColor(QStringLiteral("#cdd3de"));
    m_theme.textMuted = QColor(QStringLiteral("#96a2b4"));
    m_theme.textSubtle = QColor(QStringLiteral("#abb2bf"));
}

void ThemeManager::loadSlatePreset()
{
    m_theme.isLight = false;

    // Nordic Arctic Slate - Rich, present slate blue-gray base
    m_theme.baseBg = QColor(QStringLiteral("#222936"));
    m_theme.cardBg = QColor(QStringLiteral("#1e2430"));
    m_theme.cardBorder = QColor(QStringLiteral("#374355"));
    m_theme.dividerColor = QColor(QStringLiteral("#293344"));
    m_theme.controlBg = QColor(QStringLiteral("#2a3344"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#38465c"));
    m_theme.controlBorder = QColor(QStringLiteral("#475772"));

    m_theme.ring1Bg = QColor(QStringLiteral("#222936"));
    m_theme.ring1Border = QColor(QStringLiteral("#171c26"));
    m_theme.ring2Bg = QColor(QStringLiteral("#1d232e"));
    m_theme.ring2Border = QColor(QStringLiteral("#141820"));
    m_theme.ring3Bg = QColor(QStringLiteral("#191e27"));
    m_theme.ring3Border = QColor(QStringLiteral("#11141a"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#404f67"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#546686"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#384459"));

    m_theme.accentColor = QColor(QStringLiteral("#88c0d0"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#a3d4e2"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(110);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentColor;

    m_theme.identWindows = QColor(QStringLiteral("#a3be8c"));
    m_theme.identApps = QColor(QStringLiteral("#81a1c1"));
    m_theme.identFiles = QColor(QStringLiteral("#ebcb8b"));

    m_theme.textPrimary = QColor(QStringLiteral("#eceff4"));
    m_theme.textSecondary = QColor(QStringLiteral("#d8dee9"));
    m_theme.textMuted = QColor(QStringLiteral("#88c0d0"));
    m_theme.textSubtle = QColor(QStringLiteral("#94a3b8"));
}

void ThemeManager::loadEmeraldPreset()
{
    m_theme.isLight = false;

    // Deep Boreal Pine & Jade Forest - Rich, clearly present forest green base
    m_theme.baseBg = QColor(QStringLiteral("#142e22"));
    m_theme.cardBg = QColor(QStringLiteral("#11261d"));
    m_theme.cardBorder = QColor(QStringLiteral("#254d3a"));
    m_theme.dividerColor = QColor(QStringLiteral("#1b3b2c"));
    m_theme.controlBg = QColor(QStringLiteral("#1a3b2c"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#244f3b"));
    m_theme.controlBorder = QColor(QStringLiteral("#30644b"));

    m_theme.ring1Bg = QColor(QStringLiteral("#142e22"));
    m_theme.ring1Border = QColor(QStringLiteral("#0d1f17"));
    m_theme.ring2Bg = QColor(QStringLiteral("#11261c"));
    m_theme.ring2Border = QColor(QStringLiteral("#0b1a13"));
    m_theme.ring3Bg = QColor(QStringLiteral("#0e1f17"));
    m_theme.ring3Border = QColor(QStringLiteral("#09140f"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#2b5942"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#3e7d5d"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#244e3a"));

    m_theme.accentColor = QColor(QStringLiteral("#10b981"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#34d399"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(115);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentHoverColor;

    m_theme.identWindows = QColor(QStringLiteral("#4ade80"));
    m_theme.identApps = QColor(QStringLiteral("#2dd4bf"));
    m_theme.identFiles = QColor(QStringLiteral("#f59e0b"));

    m_theme.textPrimary = QColor(QStringLiteral("#f0fdf4"));
    m_theme.textSecondary = QColor(QStringLiteral("#d1fae5"));
    m_theme.textMuted = QColor(QStringLiteral("#6ee7b7"));
    m_theme.textSubtle = QColor(QStringLiteral("#9ca3af"));
}

void ThemeManager::loadAmethystPreset()
{
    m_theme.isLight = false;

    // Deep Velvet Royal Amethyst - Distinctly rich purple base
    m_theme.baseBg = QColor(QStringLiteral("#271b36"));
    m_theme.cardBg = QColor(QStringLiteral("#21172e"));
    m_theme.cardBorder = QColor(QStringLiteral("#472f63"));
    m_theme.dividerColor = QColor(QStringLiteral("#342249"));
    m_theme.controlBg = QColor(QStringLiteral("#332148"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#472f64"));
    m_theme.controlBorder = QColor(QStringLiteral("#5b3b80"));

    m_theme.ring1Bg = QColor(QStringLiteral("#271b36"));
    m_theme.ring1Border = QColor(QStringLiteral("#191124"));
    m_theme.ring2Bg = QColor(QStringLiteral("#21162e"));
    m_theme.ring2Border = QColor(QStringLiteral("#150e1e"));
    m_theme.ring3Bg = QColor(QStringLiteral("#1b1226"));
    m_theme.ring3Border = QColor(QStringLiteral("#100b17"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#4f326f"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#6c4596"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#432f5d"));

    m_theme.accentColor = QColor(QStringLiteral("#a855f7"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#c084fc"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(115);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentHoverColor;

    m_theme.identWindows = QColor(QStringLiteral("#34d399"));
    m_theme.identApps = QColor(QStringLiteral("#38bdf8"));
    m_theme.identFiles = QColor(QStringLiteral("#f472b6"));

    m_theme.textPrimary = QColor(QStringLiteral("#faf5ff"));
    m_theme.textSecondary = QColor(QStringLiteral("#e9d5ff"));
    m_theme.textMuted = QColor(QStringLiteral("#c084fc"));
    m_theme.textSubtle = QColor(QStringLiteral("#a8a29e"));
}

void ThemeManager::loadCappuccinoPreset()
{
    m_theme.isLight = false;

    // Warm Roasted Espresso, Mocha & Caramel - Rich, cozy coffee background
    m_theme.baseBg = QColor(QStringLiteral("#312117"));
    m_theme.cardBg = QColor(QStringLiteral("#2a1c14"));
    m_theme.cardBorder = QColor(QStringLiteral("#543827"));
    m_theme.dividerColor = QColor(QStringLiteral("#3f2a1e"));
    m_theme.controlBg = QColor(QStringLiteral("#3d291e"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#523728"));
    m_theme.controlBorder = QColor(QStringLiteral("#674532"));

    m_theme.ring1Bg = QColor(QStringLiteral("#312117"));
    m_theme.ring1Border = QColor(QStringLiteral("#21160f"));
    m_theme.ring2Bg = QColor(QStringLiteral("#2a1c14"));
    m_theme.ring2Border = QColor(QStringLiteral("#1b120c"));
    m_theme.ring3Bg = QColor(QStringLiteral("#221610"));
    m_theme.ring3Border = QColor(QStringLiteral("#150d09"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#573a2a"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#795239"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#4d3424"));

    m_theme.accentColor = QColor(QStringLiteral("#e0a96d"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#f3c28a"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(110);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentColor;

    m_theme.identWindows = QColor(QStringLiteral("#78c28e"));
    m_theme.identApps = QColor(QStringLiteral("#7ac5d8"));
    m_theme.identFiles = QColor(QStringLiteral("#f08a5d"));

    m_theme.textPrimary = QColor(QStringLiteral("#fff7ed"));
    m_theme.textSecondary = QColor(QStringLiteral("#fed7aa"));
    m_theme.textMuted = QColor(QStringLiteral("#fcd34d"));
    m_theme.textSubtle = QColor(QStringLiteral("#a8a29e"));
}

void ThemeManager::loadCrimsonPreset()
{
    m_theme.isLight = false;

    // Deep Velvet Bordeaux Wine & Radiant Rose
    m_theme.baseBg = QColor(QStringLiteral("#33141f"));
    m_theme.cardBg = QColor(QStringLiteral("#2b101a"));
    m_theme.cardBorder = QColor(QStringLiteral("#592336"));
    m_theme.dividerColor = QColor(QStringLiteral("#421927"));
    m_theme.controlBg = QColor(QStringLiteral("#401826"));
    m_theme.controlHoverBg = QColor(QStringLiteral("#572033"));
    m_theme.controlBorder = QColor(QStringLiteral("#6e2840"));

    m_theme.ring1Bg = QColor(QStringLiteral("#33141f"));
    m_theme.ring1Border = QColor(QStringLiteral("#210d14"));
    m_theme.ring2Bg = QColor(QStringLiteral("#2b101a"));
    m_theme.ring2Border = QColor(QStringLiteral("#1c0a11"));
    m_theme.ring3Bg = QColor(QStringLiteral("#230c15"));
    m_theme.ring3Border = QColor(QStringLiteral("#16070d"));

    m_theme.ringSelectionBg = QColor(QStringLiteral("#5d2338"));
    m_theme.windowSelectionBg = QColor(QStringLiteral("#822f4d"));
    m_theme.windowUnselBg = QColor(QStringLiteral("#532132"));

    m_theme.accentColor = QColor(QStringLiteral("#f43f5e"));
    m_theme.accentHoverColor = QColor(QStringLiteral("#fb7185"));
    m_theme.drilldownColor = m_theme.accentColor.lighter(115);
    m_theme.drilldownColor.setAlphaF(0.32);
    m_theme.trailColor = m_theme.accentColor;

    m_theme.identWindows = QColor(QStringLiteral("#34d399"));
    m_theme.identApps = QColor(QStringLiteral("#38bdf8"));
    m_theme.identFiles = QColor(QStringLiteral("#fb923c"));

    m_theme.textPrimary = QColor(QStringLiteral("#fff1f2"));
    m_theme.textSecondary = QColor(QStringLiteral("#fecdd3"));
    m_theme.textMuted = QColor(QStringLiteral("#fda4af"));
    m_theme.textSubtle = QColor(QStringLiteral("#9ca3af"));
}

void ThemeManager::generatePalette(const QColor &baseColor)
{
    double h = baseColor.hslHueF();
    if (h < 0.0) h = 0.58;
    double s = baseColor.hslSaturationF();
    double l = baseColor.lightnessF();

    bool isLightMode = (l > 0.55);
    m_theme.isLight = isLightMode;

    if (!isLightMode) {
        // Dark Mode: Keep background richly and prominently tinted with user's base color
        double targetS = std::clamp(s * 0.90, 0.22, 0.65);
        double targetL = std::clamp(l, 0.12, 0.22);

        m_theme.baseBg = QColor::fromHslF(h, targetS, targetL);
        m_theme.cardBg = QColor::fromHslF(h, targetS, std::max(0.08, targetL - 0.03));
        m_theme.ring1Bg = m_theme.baseBg;
        m_theme.ring2Bg = QColor::fromHslF(h, targetS, std::max(0.07, targetL - 0.025));
        m_theme.ring3Bg = QColor::fromHslF(h, targetS, std::max(0.06, targetL - 0.05));

        m_theme.cardBorder = QColor::fromHslF(h, targetS * 0.85, std::min(0.42, targetL + 0.15));
        m_theme.dividerColor = QColor::fromHslF(h, targetS * 0.80, std::min(0.32, targetL + 0.08));
        m_theme.ring1Border = QColor::fromHslF(h, targetS * 0.90, std::max(0.05, targetL - 0.06));
        m_theme.ring2Border = QColor::fromHslF(h, targetS * 0.90, std::max(0.04, targetL - 0.07));
        m_theme.ring3Border = QColor::fromHslF(h, targetS * 0.90, std::max(0.03, targetL - 0.08));

        m_theme.controlBg = QColor::fromHslF(h, targetS * 0.80, std::min(0.32, targetL + 0.06));
        m_theme.controlHoverBg = QColor::fromHslF(h, targetS * 0.80, std::min(0.42, targetL + 0.12));
        m_theme.controlBorder = QColor::fromHslF(h, targetS * 0.75, std::min(0.48, targetL + 0.18));

        m_theme.textPrimary = QColor(QStringLiteral("#ffffff"));
        m_theme.textSecondary = QColor::fromHslF(h, 0.12, 0.86);
        m_theme.textMuted = QColor::fromHslF(h, 0.18, 0.70);
        m_theme.textSubtle = QColor::fromHslF(h, 0.12, 0.52);

        m_theme.ringSelectionBg = QColor::fromHslF(h, targetS * 0.80, std::min(0.46, targetL + 0.18));
        m_theme.windowSelectionBg = QColor::fromHslF(h, targetS * 0.80, std::min(0.56, targetL + 0.24));
        m_theme.windowUnselBg = QColor::fromHslF(h, targetS * 0.85, std::min(0.40, targetL + 0.10));

        double accentH = (s > 0.20) ? h : 0.58;
        m_theme.accentColor = QColor::fromHslF(accentH, 0.85, 0.52);
        m_theme.accentHoverColor = QColor::fromHslF(accentH, 0.80, 0.60);
        m_theme.drilldownColor = m_theme.accentColor.lighter(115);
        m_theme.drilldownColor.setAlphaF(0.32);
        m_theme.trailColor = m_theme.accentHoverColor;

        m_theme.identWindows = QColor::fromHslF(0.39, 0.75, 0.51);
        m_theme.identApps = QColor::fromHslF(0.54, 0.90, 0.60);
        m_theme.identFiles = QColor::fromHslF(0.08, 0.95, 0.62);
    } else {
        // Light Mode:
        double targetS = std::clamp(s * 0.75, 0.15, 0.45);
        double targetL = std::clamp(l, 0.88, 0.96);

        m_theme.baseBg = QColor::fromHslF(h, targetS, targetL);
        m_theme.cardBg = QColor::fromHslF(h, targetS * 0.6, std::min(0.98, targetL + 0.02));
        m_theme.ring1Bg = m_theme.baseBg;
        m_theme.ring2Bg = QColor::fromHslF(h, targetS, std::max(0.82, targetL - 0.04));
        m_theme.ring3Bg = QColor::fromHslF(h, targetS, std::max(0.78, targetL - 0.07));

        m_theme.cardBorder = QColor::fromHslF(h, targetS, std::max(0.70, targetL - 0.14));
        m_theme.dividerColor = QColor::fromHslF(h, targetS, std::max(0.76, targetL - 0.08));
        m_theme.ring1Border = QColor::fromHslF(h, targetS, std::max(0.72, targetL - 0.12));
        m_theme.ring2Border = QColor::fromHslF(h, targetS, std::max(0.68, targetL - 0.15));
        m_theme.ring3Border = QColor::fromHslF(h, targetS, std::max(0.64, targetL - 0.18));

        m_theme.controlBg = QColor::fromHslF(h, targetS * 0.8, std::max(0.84, targetL - 0.05));
        m_theme.controlHoverBg = QColor::fromHslF(h, targetS, std::max(0.78, targetL - 0.10));
        m_theme.controlBorder = QColor::fromHslF(h, targetS, std::max(0.68, targetL - 0.20));

        m_theme.textPrimary = QColor(QStringLiteral("#111827"));
        m_theme.textSecondary = QColor::fromHslF(h, 0.20, 0.25);
        m_theme.textMuted = QColor::fromHslF(h, 0.25, 0.40);
        m_theme.textSubtle = QColor::fromHslF(h, 0.15, 0.55);

        m_theme.ringSelectionBg = QColor::fromHslF(h, targetS * 1.2, std::max(0.68, targetL - 0.16));
        m_theme.windowSelectionBg = QColor::fromHslF(h, targetS * 1.2, std::max(0.52, targetL - 0.26));
        m_theme.windowUnselBg = QColor::fromHslF(h, targetS * 0.90, std::max(0.68, targetL - 0.12));

        double accentH = (s > 0.20) ? h : 0.58;
        m_theme.accentColor = QColor::fromHslF(accentH, 0.85, 0.45);
        m_theme.accentHoverColor = QColor::fromHslF(accentH, 0.80, 0.38);
        m_theme.drilldownColor = m_theme.accentColor.lighter(120);
        m_theme.drilldownColor.setAlphaF(0.28);
        m_theme.trailColor = m_theme.accentColor;

        m_theme.identWindows = QColor(QStringLiteral("#16a34a"));
        m_theme.identApps = QColor(QStringLiteral("#0284c7"));
        m_theme.identFiles = QColor(QStringLiteral("#d97706"));
    }
}
