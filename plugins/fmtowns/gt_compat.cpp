// Headless-embedding compatibility for the Geartowns core.
//
// Geartowns' core logging (src/common/log.h) references `g_mcp_stdio_mode`,
// a flag the desktop application defines to silence log output when it serves
// an MCP client over stdio. Asuka embeds the core without that application, so
// the symbol must be provided here. Left false: Asuka keeps Geartowns' normal
// log output, which is useful while running a plugin from the CLI.

bool g_mcp_stdio_mode = false;
