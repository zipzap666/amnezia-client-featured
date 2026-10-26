#include "mozilla/localsocketcontroller.h"
#include <QCoreApplication>
#include <cstdio>
#include <cstdlib>
static void check(bool value, const char *name) {
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", name); std::exit(1); }
}
// Feed daemon replies into the real production parser. No initialize() call,
// no connection to the installed service, no network or settings mutations.
class LocalSocketControllerTest {
public:
    static void run() {
        LocalSocketController controller;
        int disconnected = 0, errors = 0, connected = 0;
        QObject::connect(&controller, &ControllerImpl::connected, [&] { ++connected; });
        QObject::connect(&controller, &ControllerImpl::disconnected, [&] { ++disconnected; });
        int daemonError = -1;
        QObject::connect(&controller, &LocalSocketController::backendError, [&](int code) { ++errors; daemonError = code; });
        controller.m_daemonState = LocalSocketController::eReady;
        controller.m_activationRequested = true;
        controller.deactivate();
        check(disconnected == 0, "sending deactivate is not completion");
        controller.deactivate();
        check(disconnected == 0, "repeated deactivate waits for same reply");
        controller.parseCommand(R"({"type":"disconnected"})");
        check(disconnected == 0 && controller.m_waitingForStoppedStatus, "disconnected waits for cleanup barrier");
        controller.parseCommand(R"({"type":"status","connected":false})");
        check(disconnected == 1 && !controller.m_activationRequested, "stopped status completes teardown");
        controller.m_activationRequested = true;
        controller.deactivate();
        controller.parseCommand(R"({"type":"disconnected"})");
        controller.parseCommand(R"({"type":"status","connected":true})");
        check(errors == 1 && disconnected == 1, "still connected is failure, never a restart acknowledgement");
        controller.parseCommand(R"({"type":"backendFailure","errorCode":1})");
        check(errors == 2, "backend errors propagated");
        controller.parseCommand(R"({"type":"backendFailure","errorCode":2})");
        check(errors == 3 && daemonError == 2, "split tunnel initialization failure is distinguishable from IPC failure");
        controller.parseCommand(R"({"type":"backendFailure","errorCode":3})");
        check(errors == 4 && daemonError == 3, "split tunnel start failure is preserved");
        controller.parseCommand(R"({"type":"backendFailure","errorCode":4})");
        check(errors == 5 && daemonError == 4, "split tunnel rules failure is preserved");
        controller.parseCommand(R"({"type":"connected","pubkey":"test"})");
        check(connected == 0, "late connected after failure cannot report success");
        LocalSocketController preparing;
        int cancelled = 0;
        QObject::connect(&preparing, &ControllerImpl::disconnected, [&] { ++cancelled; });
        preparing.activate({});
        check(preparing.m_pendingActivation.has_value(), "activation waits for initialization");
        preparing.deactivate();
        check(cancelled == 1 && !preparing.m_pendingActivation, "cancel removes queued activation");
        std::puts("PASS: real daemon reply parser and disconnect acknowledgement sequencing");
    }
};
int main(int argc, char **argv) { QCoreApplication app(argc, argv); LocalSocketControllerTest::run(); }
