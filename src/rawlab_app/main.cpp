#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QTimer>

int main(int argc, char **argv)
{
  QGuiApplication::setDesktopFileName(QStringLiteral("rawlab"));
  QApplication application{argc, argv};
  const bool smokeTest = application.arguments().contains(QStringLiteral("--smoke-test"));
  QCoreApplication::setApplicationName(smokeTest ? QStringLiteral("RawLabSmoke") : QStringLiteral("RawLab"));
  QCoreApplication::setOrganizationName(QStringLiteral("RawLab"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  application.setWindowIcon(QIcon{QStringLiteral(":/icons/rawlab.png")});
  MainWindow window;
  window.show();
  if(smokeTest)
  {
    QTimer::singleShot(0, &application, &QCoreApplication::quit);
  }
  return application.exec();
}
