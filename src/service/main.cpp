/*
 * Copyright (C) 2015 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 3 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: James Henstridge <james.henstridge@canonical.com>
 *              Michi Henning <michi.henning@canonical.com>
 */

#include "admininterfaceadaptor.h"
#include "admininterface.h"
#include "dbusinterfaceadaptor.h"
#include "dbusinterface.h"
#include "inactivityhandler.h"

#include <QCoreApplication>

#include <cstdio>
#include <iostream>

using namespace unity::thumbnailer::service;

static const char BUS_NAME[] = "com.canonical.Thumbnailer";
static const char BUS_PATH[] = "/com/canonical/Thumbnailer";

static const char BUS_ADMIN_NAME[] = "com.canonical.ThumbnailerAdmin";
static const char BUS_ADMIN_PATH[] = "/com/canonical/ThumbnailerAdmin";

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);

    auto bus = QDBusConnection::sessionBus();

    unity::thumbnailer::service::DBusInterface server;
    new ThumbnailerAdaptor(&server);
    bus.registerObject(BUS_PATH, &server);
    if (!bus.registerService(BUS_NAME))
    {
        fprintf(stderr, "Could not acquire D-Bus name %s.\n", BUS_NAME);
        return 0;
    }

    try
    {
        new InactivityHandler(server);
    }
    catch (std::invalid_argument& e)
    {
        std::cerr << e.what() << std::endl;
        exit(1);
    }

    unity::thumbnailer::service::AdminInterface admin_server;
    new ThumbnailerAdminAdaptor(&admin_server);
    bus.registerObject(BUS_ADMIN_PATH, &admin_server);
    if (!bus.registerService(BUS_ADMIN_NAME))
    {
        fprintf(stderr, "Could not acquire D-Bus name %s.\n", BUS_ADMIN_NAME);
        return 0;
    }
    qDBusRegisterMetaType<unity::thumbnailer::service::AllStats>();

    return app.exec();
}
