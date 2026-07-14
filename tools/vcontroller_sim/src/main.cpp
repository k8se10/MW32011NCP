// vcontroller_sim — dev-only test tool, NOT part of the shipped mod.
//
// Plugs in a virtual Xbox 360 controller via ViGEmBus (a real kernel driver, installed
// separately) so automated testing can simulate stick/button input without physical
// controller hardware in this environment. Reads simple line commands from stdin so an
// external script (PowerShell) can drive it interactively while the game is running.
//
// Commands (one per line):
//   LX <float -1..1>   -- set left stick X, applied immediately
//   LY <float -1..1>   -- set left stick Y
//   RX <float -1..1>   -- set right stick X
//   RY <float -1..1>   -- set right stick Y
//   RESET              -- zero all axes/buttons
//   QUIT                -- unplug and exit

#include <windows.h>
#include <iostream>
#include <sstream>
#include <string>
#include "../third_party/vigemclient/include/ViGEmClient.h"

namespace {
SHORT ToAxis(float v)
{
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    return static_cast<SHORT>(v * 32767.0f);
}
}

int main()
{
    PVIGEM_CLIENT client = vigem_alloc();
    if (!client) {
        std::cerr << "vigem_alloc failed\n";
        return 1;
    }

    VIGEM_ERROR err = vigem_connect(client);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "vigem_connect failed: 0x" << std::hex << err << "\n";
        return 1;
    }

    PVIGEM_TARGET pad = vigem_target_x360_alloc();
    err = vigem_target_add(client, pad);
    if (!VIGEM_SUCCESS(err)) {
        std::cerr << "vigem_target_add failed: 0x" << std::hex << err << "\n";
        return 1;
    }

    ULONG userIndex = 0;
    vigem_target_x360_get_user_index(client, pad, &userIndex);
    std::cerr << "READY xinput_user_index=" << userIndex << "\n";
    std::cout.flush();

    XUSB_REPORT report;
    XUSB_REPORT_INIT(&report);

    std::string line;
    while (std::getline(std::cin, line)) {
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "LX") { float v; iss >> v; report.sThumbLX = ToAxis(v); }
        else if (cmd == "LY") { float v; iss >> v; report.sThumbLY = ToAxis(v); }
        else if (cmd == "RX") { float v; iss >> v; report.sThumbRX = ToAxis(v); }
        else if (cmd == "RY") { float v; iss >> v; report.sThumbRY = ToAxis(v); }
        else if (cmd == "RESET") { XUSB_REPORT_INIT(&report); }
        else if (cmd == "QUIT") { break; }
        else { std::cerr << "unknown command: " << cmd << "\n"; continue; }

        vigem_target_x360_update(client, pad, report);
        std::cerr << "OK\n";
        std::cout.flush();
    }

    vigem_target_remove(client, pad);
    vigem_target_free(pad);
    vigem_disconnect(client);
    vigem_free(client);
    return 0;
}
