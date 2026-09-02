#include "MainWindow.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QTextStream>
#include <QTimer>

int main(int argc, char **argv)
{
  QGuiApplication::setDesktopFileName(QStringLiteral("rawlab"));
  QApplication application{argc, argv};
  QCoreApplication::setApplicationName(QStringLiteral("RawLab"));
  QCoreApplication::setOrganizationName(QStringLiteral("RawLab"));
  QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
  QCommandLineParser parser;
  parser.setApplicationDescription(QStringLiteral("Raw-domain photo preprocessing application"));
  parser.addHelpOption();
  parser.addVersionOption();
  const QCommandLineOption smokeTestOption{QStringLiteral("smoke-test"),
                                            QStringLiteral("Start and immediately exit")};
  const QCommandLineOption externalEditOption{QStringLiteral("external-edit"),
    QStringLiteral("Open a RAW file for an external-editor workflow"),
    QStringLiteral("input")};
  const QCommandLineOption outputOption{QStringLiteral("output"),
    QStringLiteral("Write external-editor output to this new DNG path"),
    QStringLiteral("path")};
  parser.addOption(smokeTestOption);
  parser.addOption(externalEditOption);
  parser.addOption(outputOption);
  parser.process(application);

  const bool smokeTest = parser.isSet(smokeTestOption);
  if(smokeTest) QCoreApplication::setApplicationName(QStringLiteral("RawLabSmoke"));
  application.setWindowIcon(QIcon{QStringLiteral(":/icons/rawlab.png")});
  const bool hasExternalInput = parser.isSet(externalEditOption);
  const bool hasExternalOutput = parser.isSet(outputOption);
  if(hasExternalInput != hasExternalOutput || (smokeTest && hasExternalInput))
  {
    QTextStream{stderr} << "--external-edit and --output must be used together\n";
    return 2;
  }

  QString inputPath;
  QString outputPath;
  if(hasExternalInput)
  {
    inputPath = QFileInfo{parser.value(externalEditOption)}.absoluteFilePath();
    outputPath = QFileInfo{parser.value(outputOption)}.absoluteFilePath();
    const QFileInfo inputInfo{inputPath};
    const QFileInfo outputInfo{outputPath};
    if(!inputInfo.isFile())
    {
      QTextStream{stderr} << "External-edit input is not a file: " << inputPath << '\n';
      return 2;
    }
    if(outputInfo.exists())
    {
      QTextStream{stderr} << "External-edit output already exists: " << outputPath << '\n';
      return 2;
    }
    if(!outputInfo.dir().exists() || inputPath == outputPath)
    {
      QTextStream{stderr} << "Invalid external-edit output path: " << outputPath << '\n';
      return 2;
    }
  }

  MainWindow window{inputPath, outputPath};
  window.show();
  if(smokeTest)
  {
    QTimer::singleShot(0, &application, &QCoreApplication::quit);
  }
  const int result = application.exec();
  if(hasExternalInput && result == 0 && !window.externalExportSucceeded()) return 3;
  return result;
}
