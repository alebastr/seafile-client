#include <getopt.h>
#include <QApplication>
#include <QMessageBox>
#include <QWidget>
#include <QDir>

#include <glib-object.h>
#include <cstdio>

#include "i18n.h"
#include "crash-handler.h"
#include "utils/utils.h"
#include "utils/paint-utils.h"
#include "utils/process.h"
#include "utils/uninstall-helpers.h"
#include "ui/proxy-style.h"
#include "seafile-applet.h"
#include "QtAwesome.h"
#include "open-local-helper.h"
#if defined(Q_OS_WIN32)
#include "utils/utils-win.h"
#endif
#if defined(Q_OS_MAC)
#include "application.h"
#endif

#define APPNAME "seafile-applet"

namespace {
void initGlib()
{
#if !GLIB_CHECK_VERSION(2, 35, 0)
    g_type_init();
#endif
#if !GLIB_CHECK_VERSION(2, 31, 0)
    g_thread_init(NULL);
#endif
}

#if defined(Q_OS_WIN32)
void initBreakpad()
{
#ifdef SEAFILE_CLIENT_HAS_CRASH_REPORTER
    // if we have built with breakpad, load it in run time
    Breakpad::CrashHandler::instance()->Init(
        QDir(defaultCcnetDir()).absoluteFilePath("crash-applet"));
#endif
}
#endif

void setupFontFix()
{
#if QT_VERSION < QT_VERSION_CHECK(5, 3, 2) && defined(Q_OS_MAC)
    // Text in buttons and drop-downs looks misaligned in osx 10.10,
    // fixed in qt5.3.2
    // https://bugreports.qt-project.org/browse/QTBUG-40833
    if ( QSysInfo::MacintoshVersion > QSysInfo::MV_10_8 ) {
        QFont::insertSubstitution(".Helvetica Neue DeskInterface", "Helvetica Neue");
    }
#endif // QT_VERSION_CHECK(5, 3, 2)
}

void setupHIDPIFix()
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 1, 0)
    // enable builtin retina mode
    // http://blog.qt.digia.com/blog/2013/04/25/retina-display-support-for-mac-os-ios-and-x11/
    // https://qt.gitorious.org/qt/qtbase/source/a3cb057c3d5c9ed2c12fb7542065c3d667be38b7:src/gui/image/qicon.cpp#L1028-1043
    qApp->setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

#if QT_VERSION >= QT_VERSION_CHECK(5, 6, 0)
  #if defined(Q_OS_WIN32)
    if (!utils::win::fixQtHDPINonIntegerScaling()) {
        qApp->setAttribute(Qt::AA_EnableHighDpiScaling);
    }
  #elif !defined(Q_OS_MAC)
    // Enable HDPI auto detection.
    // See http://blog.qt.io/blog/2016/01/26/high-dpi-support-in-qt-5-6/
    qApp->setAttribute(Qt::AA_EnableHighDpiScaling);
  #endif
#endif
}

void setupSettingDomain()
{
    // see QSettings documentation
    QCoreApplication::setOrganizationName(getBrand());
    QCoreApplication::setOrganizationDomain("seafile.com");
    QCoreApplication::setApplicationName(QString("%1 Client").arg(getBrand()));
}

void handleCommandLineOption(int argc, char *argv[])
{
    int c;
    static const char *short_options = "KDXPc:d:f:";
    static const struct option long_options[] = {
        { "config-dir", required_argument, NULL, 'c' },
        { "data-dir", required_argument, NULL, 'd' },
        { "stop", no_argument, NULL, 'K' },
        { "delay", no_argument, NULL, 'D' },
        { "remove-user-data", no_argument, NULL, 'X' },
        { "open-local-file", no_argument, NULL, 'f' },
        { "stdout", no_argument, NULL, 'l' },
        { "ping", no_argument, NULL, 'P' },
        { NULL, 0, NULL, 0, },
    };

    while ((c = getopt_long (argc, argv, short_options,
                             long_options, NULL)) != EOF) {
        switch (c) {
        case 'c':
            g_setenv ("CCNET_CONF_DIR", optarg, 1);
            break;
        case 'd':
            g_setenv ("SEAFILE_DATA_DIR", optarg, 1);
            break;
        case 'l':
            g_setenv ("LOG_STDOUT", "", 1);
            break;
        case 'K':
            do_stop();
            exit(0);
        case 'P':
            do_ping();
            exit(0);
        case 'D':
            msleep(1000);
            break;
        case 'X':
            do_remove_user_data();
            exit(0);
        case 'f':
            OpenLocalHelper::instance()->handleOpenLocalFromCommandLine(optarg);
            break;
#if defined(HAVE_SPARKLE_SUPPORT) && defined(WINSPARKLE_DEBUG)
        case 'U':
            g_setenv ("SEAFILE_CLIENT_APPCAST_URI", optarg, 1);
            break;
#endif
        default:
            exit(1);
        }
    }

}

} // anonymous namespace

int main(int argc, char *argv[])
{
    int ret = 0;

    // call glib's init functions
    initGlib();

    // initialize breakpad if enabled
#if defined(Q_OS_WIN32)
    initBreakpad();
#endif

    // Apply hidpi support
    setupHIDPIFix();

#if defined(Q_OS_WIN32)
    // When the user start seafile applet from the windows "Start" menu, the
    // working directory is set to the parent folder of the seafile-applet.exe.
    // See https://github.com/haiwen/seafile/blob/v6.0.1/msi/seafile.wxs#L60
    //
    // Sometimes the seafile-applet program would abort with error messgages
    // "can't find qt plugin windows dll". So here we add current directory to
    // the library path, hopefully fixing that problem.
    QCoreApplication::addLibraryPath(".\\");
#endif

    // TODO imple if we have to restart the application
    // the manual at http://qt-project.org/wiki/ApplicationRestart
#if defined(Q_OS_MAC)
    Application app(argc, argv);
#else
    QApplication app(argc, argv);
#endif

    // don't quit even if the last windows is closed
    app.setQuitOnLastWindowClosed(false);

    // apply some ui fixes for mac
    setupFontFix();

    // set the domains of settings
    setupSettingDomain();

    // initialize i18n settings
    I18NHelper::getInstance()->init();

    // initialize style settings
    app.setStyle(new SeafileProxyStyle());

    // start applet
    SeafileApplet mApplet;
    seafApplet = &mApplet;

    // handle with the command arguments
    handleCommandLineOption(argc, argv);

    // count if we have any instance running now. if more than one, exit
    if (count_process(APPNAME) > 1) {
        if (OpenLocalHelper::instance()->activateRunningInstance()) {
            printf("Activated running instance of seafile client\n");
            return 0;
        }
        QMessageBox::warning(NULL, getBrand(),
                             QObject::tr("%1 Client is already running").arg(getBrand()),
                             QMessageBox::Ok);
        return -1;
    }

    // init qtawesome component
    awesome = new QtAwesome(qApp);
    awesome->initFontAwesome();

    seafApplet->start();

    // qWarning("globalDevicePixelRatio() = %f\n", globalDevicePixelRatio());
    // printf("globalDevicePixelRatio() = %f\n", globalDevicePixelRatio());
    // fflush(stdout);

    // start qt eventloop
    ret = app.exec();

    return ret;
}
