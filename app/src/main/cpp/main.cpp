#include <string>
#include "cli.h"
#include "mcp.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        return run_interactive();
    }

    std::string c = argv[1];

    if (c == "menu" || c == "interactive") return run_interactive();
    if (c == "mcp") return run_mcp_server();
    if (c == "ping") return cmd_ping();
    if (c == "info") return cmd_info();
    if (c == "find") return cmd_find(argc, argv);
    if (c == "read") return cmd_read(argc, argv);
    if (c == "write") return cmd_write(argc, argv);
    if (c == "remove") return cmd_remove();
    if (c == "monitor") return cmd_monitor(argc, argv);

    usage();
    return 2;
}