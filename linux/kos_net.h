/* Linux stand-in for the SDK's network setup helpers: the loopback interface is already up. */
#define DEFAULT_INTERFACE "lo"
#define IWF_EXISTS 0
#define DEFAULT_TIMEOUT 0
#define DEFAULT_ADDR 0
#define DEFAULT_MASK 0
#define DEFAULT_GATEWAY 0
#define DEFAULT_MTU 0
static int wait_for_iface(const char* a, int b, int c) { (void)a; (void)b; (void)c; return 1; }
static int configure_net_iface(const char* a, int b, int c, int d, int e) { (void)a; (void)b; (void)c; (void)d; (void)e; return 1; }
