#include <QCoreApplication>
#include <QTimer>

int test_main_asyncop();

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    // Start tests immediately after event loop starts
    QTimer::singleShot(0, []() {
        test_main_asyncop();
        QCoreApplication::exit(0);
    });

    return app.exec();
}
