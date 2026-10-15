#pragma once

#include <QString>

class PermissionManager
{
public:
    static bool checkUInputAccess();
    static bool checkEventDeviceAccess();
    static bool hasRequiredPermissions();

    static bool grantPermissions(QString &errorOut);
    static bool revokePermissions(QString &errorOut);
};
