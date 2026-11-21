#include "theme/Theme.h"
#include "window/MainWindow.h"

#include <plynth/core/Version.h>

#include <QApplication>
#include <QCommandLineParser>
#include <QImage>
#include <QRegularExpression>
#include <QTimer>

namespace {

constexpr int kExitSaved = 0;
constexpr int kExitSaveFailed = 1;
constexpr int kExitBadArguments = 2;
constexpr int kExitImageFailed = 3;

// Reads "WxH" into a size. Returns false when the text is not two positive numbers.
bool parseSize(const QString& text, QSize& out)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d+)x(\\d+)$"));
    const QRegularExpressionMatch m = pattern.match(text);
    if (!m.hasMatch())
        return false;
    const int w = m.captured(1).toInt();
    const int h = m.captured(2).toInt();
    if (w < 1 || h < 1)
        return false;
    out = QSize(w, h);
    return true;
}

// Saves a picture of the window at exactly the requested pixel size.
bool saveScreenshot(QWidget& window, const QString& file, const QSize& size)
{
    QImage image = window.grab().toImage();
    if (image.size() != size)
        image = image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    image.setDevicePixelRatio(1.0);
    return image.save(file, "PNG");
}

}

// Parses the options, opens the window and optionally saves a screenshot.
int main(int argc, char* argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("Plynth"));
    QApplication::setApplicationVersion(QString::fromUtf8(plynth::version().data(), static_cast<qsizetype>(plynth::version().size())));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Plynth"));
    parser.addPositionalArgument(QStringLiteral("project"),
                                 QStringLiteral("Project folder to open (optional)."));
    QCommandLineOption screenshotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a picture of the window and exit."),
                                        QStringLiteral("file"));
    QCommandLineOption sizeOption(QStringLiteral("size"),
                                  QStringLiteral("Window size in pixels, for example 1280x720."),
                                  QStringLiteral("WxH"));
    QCommandLineOption imageOption(QStringLiteral("image"),
                                   QStringLiteral("Plan image to open."),
                                   QStringLiteral("file"));
    parser.addOption(imageOption);
    parser.addOption(screenshotOption);
    parser.addOption(sizeOption);
    if (!parser.parse(QApplication::arguments()))
        return kExitBadArguments;

    QSize size;
    const bool hasSize = parser.isSet(sizeOption);
    if (hasSize && !parseSize(parser.value(sizeOption), size))
        return kExitBadArguments;
    const bool wantsScreenshot = parser.isSet(screenshotOption);
    if (wantsScreenshot && parser.value(screenshotOption).isEmpty())
        return kExitBadArguments;

    plynth::app::Theme& theme = plynth::app::Theme::instance();
    theme.load(QStringLiteral(":/themes/drafting-dark.json"));
    theme.loadBundledFonts();
    theme.apply();

    const QStringList positional = parser.positionalArguments();
    plynth::app::MainWindow window(positional.isEmpty() ? QString() : positional.first());
    if (hasSize)
        window.resize(size);
    else
        window.resize(1280, 800);
    QString imageError;
    bool imageFailed = false;
    if (parser.isSet(imageOption))
        imageFailed = !window.loadImage(parser.value(imageOption), &imageError);
    window.show();
    if (imageFailed && !wantsScreenshot)
        QTimer::singleShot(0, &window, [&window, imageError]() { window.showImageError(imageError); });

    if (wantsScreenshot) {
        const QString file = parser.value(screenshotOption);
        const QSize target = hasSize ? size : window.size();
        QTimer::singleShot(400, &app, [&window, file, target, imageFailed]() {
            const bool ok = saveScreenshot(window, file, target);
            QApplication::exit(!ok ? kExitSaveFailed : imageFailed ? kExitImageFailed : kExitSaved);
        });
    }
    return app.exec();
}
